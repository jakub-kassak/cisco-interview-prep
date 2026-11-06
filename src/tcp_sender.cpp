#include "tcp_sender.h"

#include "transport_limits.h"

#include <QTcpSocket>
#include <QtEndian>

TcpSender::TcpSender(QObject *parent)
    : QObject(parent)
{
}

TcpSender::~TcpSender() = default;

void TcpSender::connectToHost(const QString &host, quint16 port)
{
    // A new socket gives every connection its own callbacks and write buffer.
    // Delayed events from an old connection must not affect the new session.
    auto *oldSocket = socket_;
    socket_ = nullptr;
    hadPending_ = false;
    if (oldSocket) {
        const bool wasConnected = oldSocket->state() == QAbstractSocket::ConnectedState;
        oldSocket->abort();
        oldSocket->deleteLater();
        if (wasConnected) {
            emit disconnected();
            // A signal handler may already have started another connection.
            if (socket_) {
                return;
            }
        }
    }

    auto *socket = new QTcpSocket(this);
    socket_ = socket;
    connect(socket, &QTcpSocket::connected, this, [this, socket] {
        if (socket_ == socket) {
            emit connected();
        }
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        if (socket_ == socket) {
            hadPending_ = false;
            emit disconnected();
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, socket](QAbstractSocket::SocketError) {
        if (socket_ == socket) {
            emit errorOccurred(socket->errorString());
        }
    });
    connect(socket, &QTcpSocket::bytesWritten, this, [this, socket](qint64) {
        if (socket_ == socket && hadPending_ && socket->bytesToWrite() == 0) {
            hadPending_ = false;
            emit drained();
        }
    });
    socket->connectToHost(host, port);
}

void TcpSender::disconnectFromHost()
{
    // Explicit cancellation: discard outstanding bytes rather than waiting
    // indefinitely for a slow receiver. There is no delivery guarantee here.
    hadPending_ = false;
    if (socket_) {
        socket_->abort();
    }
}

bool TcpSender::sendMessage(QByteArray payload)
{
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState
        || payload.isEmpty() || payload.size() > TransportLimits::maxPayloadBytes) {
        return false;
    }

    const qint64 messageBytes = 4 + payload.size();
    const qint64 pendingBytes = socket_->bytesToWrite();
    if (messageBytes > TransportLimits::maxPendingBytes - pendingBytes) {
        return false;
    }

    // Submit the complete frame to Qt's write buffer. Qt handles sending it
    // over the network asynchronously, in the order of our write() calls.
    QByteArray message(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), message.data());
    message.append(payload);
    auto *socket = socket_;
    const qint64 accepted = socket->write(message);
    // An error callback may have closed or replaced the connection.
    if (socket_ != socket || socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    if (accepted != message.size()) {
        // Never append a later message after a frame that was only partly
        // accepted: the receiver could interpret its bytes as missing payload.
        hadPending_ = false;
        socket->abort();
        emit errorOccurred(QStringLiteral("Could not buffer the complete TCP message."));
        return false;
    }
    hadPending_ = true;
    return true;
}
