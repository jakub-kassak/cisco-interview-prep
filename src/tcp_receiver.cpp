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
    disconnectPeer();
}

void TcpReceiver::disconnectPeer()
{
    if (peer_) {
        discardPeer(peer_);
        emit disconnected();
    } else {
        resetMessage();
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
    discardPeer(socket);
    emit errorOccurred(reason);
    emit disconnected();
}

void TcpReceiver::discardPeer(QTcpSocket *socket)
{
    // Clear state before callbacks can reconnect or restart the listener.
    peer_ = nullptr;
    resetMessage();
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
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

        acceptPeer(socket);
    }
}

void TcpReceiver::acceptPeer(QTcpSocket *socket)
{
    peer_ = socket;
    resetMessage();
    socket->setReadBufferSize(socketBufferBytes);
    connect(socket, &QTcpSocket::readyRead, this,
            [this, socket] { readMessages(socket); });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, socket](QAbstractSocket::SocketError error) {
        // Normal EOF is handled by disconnected(), not as an error.
        if (peer_ == socket && error != QAbstractSocket::RemoteHostClosedError) {
            emit errorOccurred(socket->errorString());
        }
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        readMessages(socket); // Drain complete messages buffered before EOF.
        if (peer_ == socket) {
            disconnectPeer();
        }
    });
    emit connected();
    readMessages(socket);
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
    if (socket->bytesAvailable() == 0) {
        return std::nullopt;
    }
    if (expectedBytes_ == 0) {
        header_.append(socket->read(headerBytes - header_.size()));
        if (header_.size() != headerBytes) {
            return std::nullopt;
        }
        expectedBytes_ = qFromBigEndian<quint32>(header_.constData());
        if (expectedBytes_ == 0 || expectedBytes_ > TransportLimits::maxPayloadBytes) {
            return std::unexpected(
                QStringLiteral("Invalid payload length (allowed: 1–16 MiB)."));
        }
        payload_.reserve(expectedBytes_);
    }
    while (payload_.size() < expectedBytes_ && socket->bytesAvailable() > 0) {
        const qint64 remaining = static_cast<qint64>(expectedBytes_) - payload_.size();
        const QByteArray bytes = socket->read(std::min(remaining, socketBufferBytes));
        if (bytes.isEmpty()) {
            return std::nullopt;
        }
        payload_.append(bytes);
    }
    if (payload_.size() != expectedBytes_) {
        return std::nullopt;
    }
    QByteArray message = std::move(payload_);
    resetMessage();
    return std::optional<QByteArray>{std::move(message)};
}
