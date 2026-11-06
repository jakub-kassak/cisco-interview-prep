#include "tcp_receiver.h"
#include "tcp_sender.h"
#include "transport_limits.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QtEndian>

namespace {
QByteArray frame(const QByteArray &payload)
{
    QByteArray bytes(4, '\0');
    qToBigEndian<quint32>(quint32(payload.size()), bytes.data());
    bytes += payload;
    return bytes;
}
}

class TransportTests : public QObject
{
    Q_OBJECT
private slots:
    void senderProducesExpectedWireBytes()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        TcpSender sender;
        QVERIFY(!sender.sendMessage("not connected"));
        QSignalSpy connected(&sender, &TcpSender::connected);
        QSignalSpy drained(&sender, &TcpSender::drained);
        sender.connectToHost("127.0.0.1", server.serverPort());
        QTRY_COMPARE(connected.count(), 1);
        QTRY_VERIFY(server.hasPendingConnections());
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QVERIFY(!sender.sendMessage({}));
        QVERIFY(!sender.sendMessage(QByteArray(TransportLimits::maxPayloadBytes + 1, 'x')));
        const QByteArray binary("a\0b\xff", 4);
        QVERIFY(sender.sendMessage(binary));
        QVERIFY(sender.sendMessage("second"));
        const QByteArray expected = frame(binary) + frame("second");
        QTRY_COMPARE(peer->bytesAvailable(), expected.size());
        QCOMPARE(peer->readAll(), expected);
        QTRY_VERIFY(drained.count() >= 1);
    }

    void senderLimitsBufferAndAcceptsMoreAfterDraining()
    {
        TcpReceiver receiver;
        QVERIFY(receiver.listen(QHostAddress::LocalHost, 0));
        QSignalSpy messages(&receiver, &TcpReceiver::messageReceived);
        TcpSender sender;
        QSignalSpy connected(&sender, &TcpSender::connected);
        QSignalSpy drained(&sender, &TcpSender::drained);
        sender.connectToHost("127.0.0.1", receiver.serverPort());
        QTRY_COMPARE(connected.count(), 1);

        const QByteArray payload(TransportLimits::maxPayloadBytes, 'x');
        QVERIFY(sender.sendMessage(payload));
        // No event-loop turn between sends: both framed messages together
        // exceed 32 MiB because each includes its four-byte length header.
        QVERIFY(!sender.sendMessage(payload));
        QTRY_COMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(0).toByteArray(), payload);
        QTRY_COMPARE(drained.count(), 1);

        QVERIFY(sender.sendMessage("after drain"));
        QTRY_COMPARE(messages.count(), 2);
        QCOMPARE(messages.at(1).at(0).toByteArray(), QByteArray("after drain"));
        QTRY_COMPARE(drained.count(), 2);
    }

    void receiverHandlesSplitAndCombinedMessages()
    {
        TcpReceiver receiver;
        QVERIFY(receiver.listen(QHostAddress::LocalHost, 0));
        QSignalSpy messages(&receiver, &TcpReceiver::messageReceived);
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, receiver.serverPort());
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        const QByteArray first = frame("first");
        socket.write(first.left(2)); // Incomplete header.
        socket.flush();
        QTest::qWait(20);
        QCOMPARE(messages.count(), 0);
        socket.write(first.mid(2, 4)); // Header plus incomplete payload.
        socket.flush();
        QTest::qWait(20);
        QCOMPARE(messages.count(), 0);
        socket.write(first.mid(6) + frame("second") + frame("third"));
        QTRY_COMPARE(messages.count(), 3);
        QCOMPARE(messages.at(0).at(0).toByteArray(), QByteArray("first"));
        QCOMPARE(messages.at(1).at(0).toByteArray(), QByteArray("second"));
        QCOMPARE(messages.at(2).at(0).toByteArray(), QByteArray("third"));
    }

    void invalidLengthAndTruncationAllowNewConnection()
    {
        TcpReceiver receiver;
        QVERIFY(receiver.listen(QHostAddress::LocalHost, 0));
        QSignalSpy errors(&receiver, &TcpReceiver::errorOccurred);
        QSignalSpy messages(&receiver, &TcpReceiver::messageReceived);
        QSignalSpy disconnected(&receiver, &TcpReceiver::disconnected);
        for (const quint32 invalid : {quint32(0), TransportLimits::maxPayloadBytes + 1}) {
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, receiver.serverPort());
            QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
            QByteArray header(4, '\0');
            qToBigEndian(invalid, header.data());
            socket.write(header);
            QTRY_COMPARE(socket.state(), QAbstractSocket::UnconnectedState);
        }
        QCOMPARE(errors.count(), 2);
        QCOMPARE(messages.count(), 0);
        {
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, receiver.serverPort());
            QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
            socket.write(frame("unfinished").left(7));
            socket.disconnectFromHost();
            QTRY_VERIFY(disconnected.count() >= 3);
        }
        TcpSender sender;
        QSignalSpy connected(&sender, &TcpSender::connected);
        sender.connectToHost("127.0.0.1", receiver.serverPort());
        QTRY_COMPARE(connected.count(), 1);
        QVERIFY(sender.sendMessage("fresh session"));
        QTRY_COMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(0).toByteArray(), QByteArray("fresh session"));
    }

    void largeBinaryRoundTrip()
    {
        TcpReceiver receiver;
        QVERIFY(receiver.listen(QHostAddress::LocalHost, 0));
        QSignalSpy messages(&receiver, &TcpReceiver::messageReceived);
        TcpSender sender;
        QSignalSpy connected(&sender, &TcpSender::connected);
        sender.connectToHost("127.0.0.1", receiver.serverPort());
        QTRY_COMPARE(connected.count(), 1);
        QByteArray data(2 * 1024 * 1024, '\0');
        for (qsizetype i = 0; i < data.size(); ++i) {
            data[i] = char(i % 251);
        }
        QVERIFY(sender.sendMessage(data));
        QTRY_COMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(0).toByteArray(), data);
    }
};

QTEST_GUILESS_MAIN(TransportTests)
#include "transport_tests.moc"
