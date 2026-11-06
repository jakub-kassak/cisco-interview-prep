#include "tcp_receiver.h"

#include "transport_limits.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>

#include <algorithm>
#include <utility>

namespace {
constexpr qint64 headerBytes = sizeof(quint32);
constexpr qint64 socketBufferBytes = 64 * 1024;
}

TcpReceiver::TcpReceiver(QObject *parent)
    : QObject(parent)
    , server_(new QTcpServer(this))
{
    connect(server_, &QTcpServer::newConnection,
            this, &TcpReceiver::acceptConnections);
    connect(server_, &QTcpServer::acceptError, this,
            [this](QAbstractSocket::SocketError) {
                emit errorOccurred(server_->errorString());
            });
}

TcpReceiver::~TcpReceiver() = default;

bool TcpReceiver::listen(const QHostAddress &address, quint16 port)
{
    close();
    if (!server_->listen(address, port)) {
        emit errorOccurred(server_->errorString());
        return false;
    }
    return true;
}

quint16 TcpReceiver::serverPort() const
{
    return server_->serverPort();
}

void TcpReceiver::close()
{
    server_->close();
    resetMessage();
    if (peer_) {
        QTcpSocket *oldPeer = peer_;
        peer_ = nullptr;
        oldPeer->disconnect(this);
        oldPeer->abort();
        oldPeer->deleteLater();
        emit disconnected();
    }
}

void TcpReceiver::resetMessage()
{
    header_.clear();
    payload_.clear();
    expectedBytes_ = 0;
}

void TcpReceiver::rejectPeer(QTcpSocket *socket, const QString &reason)
{
    // Detach before notifying callers, which may reconnect immediately.
    peer_ = nullptr;
    resetMessage();
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();

    emit errorOccurred(reason);
    emit disconnected();
}

void TcpReceiver::acceptConnections()
{
    while (server_->hasPendingConnections()) {
        QTcpSocket *socket = server_->nextPendingConnection();
        if (peer_) {
            socket->abort();
            socket->deleteLater();
            continue;
        }

        peer_ = socket;
        resetMessage();
        // The message assembly buffer is separately capped at maxPayloadBytes.
        socket->setReadBufferSize(socketBufferBytes);
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { readMessages(socket); });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, socket](QAbstractSocket::SocketError error) {
                    // Normal EOF is reported through disconnected(), not as an error.
                    if (peer_ == socket && error != QAbstractSocket::RemoteHostClosedError) {
                        emit errorOccurred(socket->errorString());
                    }
                });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            // EOF may arrive with a complete last message still buffered.
            readMessages(socket);
            if (peer_ != socket) {
                return;
            }
            peer_ = nullptr;
            resetMessage();
            socket->deleteLater();
            emit disconnected();
        });

        emit connected();
        readMessages(socket);
    }
}

void TcpReceiver::readMessages(QTcpSocket *socket)
{
    while (peer_ == socket) {
        auto result = tryReadMessage(socket);
        if (!result) {
            rejectPeer(socket, result.error());
            return;
        }
        if (!result->has_value()) {
            return; // Incomplete: resume when more bytes arrive.
        }

        emit messageReceived(std::move(result->value()));
    }
}

TcpReceiver::ReadResult TcpReceiver::tryReadMessage(QTcpSocket *socket)
{
    while (socket->bytesAvailable() > 0) {
        if (expectedBytes_ == 0) {
            const QByteArray bytes = socket->read(headerBytes - header_.size());
            if (bytes.isEmpty()) {
                return std::nullopt;
            }
            header_.append(bytes);
            if (header_.size() != headerBytes) {
                continue;
            }

            expectedBytes_ = qFromBigEndian<quint32>(header_.constData());
            if (expectedBytes_ == 0 || expectedBytes_ > TransportLimits::maxPayloadBytes) {
                return std::unexpected(
                    QStringLiteral("Invalid payload length (allowed: 1–16 MiB)."));
            }
            payload_.reserve(expectedBytes_);
        }

        const qint64 remaining = static_cast<qint64>(expectedBytes_) - payload_.size();
        const QByteArray bytes = socket->read(std::min(remaining, socketBufferBytes));
        if (bytes.isEmpty()) {
            return std::nullopt;
        }
        payload_.append(bytes);
        if (payload_.size() == expectedBytes_) {
            QByteArray message = std::move(payload_);
            resetMessage();
            return std::optional<QByteArray>{std::move(message)};
        }
    }
    return std::nullopt;
}
