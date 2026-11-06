#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>

#include <expected>
#include <optional>

class QTcpServer;
class QTcpSocket;

// The owner keeps this receiver alive throughout signal callbacks.
// Callbacks may close the connection, but must defer destruction of this object
// or its owner until the callback chain returns (for example, via deleteLater()).
class TcpReceiver : public QObject
{
    Q_OBJECT
public:
    explicit TcpReceiver(QObject *parent = nullptr);
    ~TcpReceiver() override;

    // Single peer. Additional connections are rejected while a peer is active.
    // Call all methods on the owning thread, with an active event loop.
    bool listen(const QHostAddress &address, quint16 port);
    quint16 serverPort() const; // Supports ephemeral port 0 for tests.
    void close();
    // End the active peer connection while keeping the listening socket open.
    void disconnectPeer();

signals:
    void connected();
    void disconnected();
    void errorOccurred(QString message);
    // Emitted in wire order, only for complete payloads of a valid size.
    void messageReceived(QByteArray payload);

private:
    // Success: a complete message, or nullopt when more bytes are needed.
    // Failure: a protocol error. The caller decides how to close the peer.
    using ReadResult = std::expected<std::optional<QByteArray>, QString>;

    void acceptConnections();
    void readMessages(QTcpSocket *socket);
    ReadResult tryReadMessage(QTcpSocket *socket);
    void rejectPeer(QTcpSocket *socket, const QString &reason);
    void resetMessage();

    QTcpServer *server_;
    QTcpSocket *peer_ = nullptr;
    QByteArray header_;
    QByteArray payload_;
    quint32 expectedBytes_ = 0;
};
