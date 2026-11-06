#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;

class TcpSender : public QObject
{
    Q_OBJECT
public:
    explicit TcpSender(QObject *parent = nullptr);
    ~TcpSender() override;

    // Call all methods on this object's owning thread, with an active event loop.
    void connectToHost(const QString &host, quint16 port);
    // Cancel the connection immediately, discarding pending outgoing bytes.
    void disconnectFromHost();
    // true = accepted locally, not acknowledged by the peer.
    // false = disconnected, invalid size, pending-byte limit, or write failure.
    bool sendMessage(QByteArray payload);

signals:
    void connected();
    void disconnected();
    void errorOccurred(QString message);
    // Current connection's queued application bytes have left Qt's write buffer.
    // Does not imply acknowledgement or rendering by the peer.
    void drained();

private:
    QTcpSocket *socket_ = nullptr;
    bool hadPending_ = false;
};
