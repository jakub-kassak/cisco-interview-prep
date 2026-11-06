#include "h264_encoder.h"
#include "h264_decoder.h"
#include "stream_session.h"
#include "stream_worker.h"
#include "tcp_sender.h"
#include "transport_limits.h"

#include <QCoreApplication>
#include <QMutex>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <QTest>

namespace {
QMutex warningMutex;
QStringList threadWarnings;
QtMessageHandler oldHandler = nullptr;
void captureWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.contains("different thread") || message.contains("Timers cannot") ||
        message.contains("QThread: Destroyed")) {
        QMutexLocker locker(&warningMutex);
        threadWarnings.append(message);
    }
    if (oldHandler) {
        oldHandler(type, context, message);
    }
}

QImage testFrame(QSize size, QColor color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}
}

class WorkerProbe : public QObject
{
    Q_OBJECT
signals:
    void listening(quint16 port);
    void imageReady(QImage image, quint64 ticket);
    void frameDecoded();
    void connectionChanged(bool connected);
    void frameProcessed(bool sent);
    void errorOccurred(QString message);
};

namespace {
struct WorkerThread {
    QThread thread;
    StreamWorker *worker;
    explicit WorkerThread(StreamConfig config) : worker(new StreamWorker(config))
    {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::started, worker, &StreamWorker::start);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    }
    ~WorkerThread()
    {
        if (thread.isRunning()) {
            QMetaObject::invokeMethod(worker, &StreamWorker::stop, Qt::BlockingQueuedConnection);
            thread.quit();
            thread.wait();
        }
    }
};
}

class StreamTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        oldHandler = qInstallMessageHandler(captureWarnings);
    }

    void threadedRoundTripAndFreshReconnect()
    {
        StreamConfig receiveConfig;
        receiveConfig.port = 0;
        receiveConfig.frameSize = QSize(320, 240);
        StreamSession receiver(receiveConfig);
        QSignalSpy listening(&receiver, &StreamSession::listening);
        QSignalSpy images(&receiver, &StreamSession::imageReady);
        QSignalSpy receiveErrors(&receiver, &StreamSession::errorOccurred);
        bool imageOnMainThread = false;
        connect(&receiver, &StreamSession::imageReady, this, [&] {
            imageOnMainThread = QThread::currentThread() == QCoreApplication::instance()->thread();
        });
        receiver.start();
        QTRY_COMPARE(listening.count(), 1);

        StreamConfig sendConfig = receiveConfig;
        sendConfig.role = StreamRole::Sender;
        sendConfig.port = listening.at(0).at(0).value<quint16>();
        StreamSession sender(sendConfig);
        QSignalSpy sendErrors(&sender, &StreamSession::errorOccurred);
        QSignalSpy processed(&sender, &StreamSession::frameProcessed);
        QVERIFY(!sender.submitFrame(testFrame(sendConfig.frameSize, Qt::red)));
        for (int session = 0; session < 2; ++session) {
            sender.start();
            QTRY_VERIFY(sender.isConnected());
            QVERIFY(sender.submitFrame(testFrame(sendConfig.frameSize, session ? Qt::blue : Qt::red)));
            // The GUI has not processed the worker acknowledgement yet.
            QVERIFY(!sender.submitFrame(testFrame(sendConfig.frameSize, Qt::green)));
            QTRY_COMPARE(processed.count(), session + 1);
            QVERIFY(processed.at(session).at(0).toBool());
            QTRY_COMPARE(images.count(), session + 1);
            const QImage image = images.at(session).at(0).value<QImage>();
            QCOMPARE(image.size(), sendConfig.frameSize);
            if (session == 0) {
                QVERIFY(image.pixelColor(80, 80).red() > 220);
            } else {
                QVERIFY(image.pixelColor(80, 80).blue() > 220);
            }
            QVERIFY(imageOnMainThread);
            sender.stop();
            QVERIFY(!sender.isRunning());
            QTRY_VERIFY(!receiver.isConnected());
        }
        receiver.stop();
        QVERIFY(receiveErrors.isEmpty());
        QVERIFY(sendErrors.isEmpty());
    }

    void malformedVideoDisconnectsOnlyThePeer()
    {
        StreamConfig receiverConfig;
        receiverConfig.port = 0;
        receiverConfig.frameSize = QSize(320, 240);
        StreamSession receiver(receiverConfig);
        QSignalSpy listening(&receiver, &StreamSession::listening);
        QSignalSpy errors(&receiver, &StreamSession::errorOccurred);
        QSignalSpy images(&receiver, &StreamSession::imageReady);
        receiver.start();
        QTRY_COMPARE(listening.count(), 1);
        const quint16 port = listening.at(0).at(0).value<quint16>();
        TcpSender invalidSender;
        QSignalSpy connected(&invalidSender, &TcpSender::connected);
        QSignalSpy disconnected(&invalidSender, &TcpSender::disconnected);
        invalidSender.connectToHost("127.0.0.1", port);
        QTRY_COMPARE(connected.count(), 1);
        QVERIFY(invalidSender.sendMessage(QByteArray("\0\0\0\1\xFF\xFF\xFF\xFF", 8)));
        QTRY_COMPARE(errors.count(), 1);
        QTRY_COMPARE(disconnected.count(), 1);
        QTRY_VERIFY(!receiver.isConnected());
        StreamConfig senderConfig = receiverConfig;
        senderConfig.role = StreamRole::Sender;
        senderConfig.port = port;
        StreamSession sender(senderConfig);
        sender.start();
        QTRY_VERIFY(sender.isConnected());
        QVERIFY(sender.submitFrame(testFrame(senderConfig.frameSize, Qt::green)));
        QTRY_COMPARE(images.count(), 1);
        QVERIFY(images.at(0).at(0).value<QImage>().pixelColor(80, 80).green() > 220);
        QCOMPARE(errors.count(), 1);
    }

    void shutdownDuringQueuedCaptureAndIdleListening()
    {
        StreamConfig receiverConfig;
        receiverConfig.port = 0;
        StreamSession receiver(receiverConfig);
        QSignalSpy listening(&receiver, &StreamSession::listening);
        receiver.start();
        QTRY_COMPARE(listening.count(), 1);
        StreamConfig senderConfig = receiverConfig;
        senderConfig.role = StreamRole::Sender;
        senderConfig.port = listening.at(0).at(0).value<quint16>();
        {
            StreamSession sender(senderConfig);
            sender.start();
            QTRY_VERIFY(sender.isConnected());
            QVERIFY(sender.submitFrame(testFrame(senderConfig.frameSize, Qt::green)));
            // Destructor waits for the finite job and releases sockets/codecs
            // on their owning worker thread; no GUI event pumping is needed.
        }
        QTRY_VERIFY(!receiver.isConnected());
        receiver.stop();
        receiver.start();
        QTRY_COMPARE(listening.count(), 2);
        receiver.stop();
    }

    void receiverCoalescesImagesUntilAcknowledged()
    {
        // Direct worker test: hold GUI acknowledgement while several decoded
        // pictures arrive. Only the first and then latest picture may escape.
        StreamConfig config;
        config.port = 0;
        config.frameSize = QSize(320, 240);
        WorkerThread workerThread(config);
        StreamWorker *worker = workerThread.worker;
        WorkerProbe probe;
        connect(worker, &StreamWorker::listening, &probe, &WorkerProbe::listening);
        connect(worker, &StreamWorker::imageReady, &probe, &WorkerProbe::imageReady);
        connect(worker, &StreamWorker::frameDecoded, &probe, &WorkerProbe::frameDecoded);
        QSignalSpy listening(&probe, &WorkerProbe::listening);
        QSignalSpy images(&probe, &WorkerProbe::imageReady);
        QSignalSpy decoded(&probe, &WorkerProbe::frameDecoded);
        workerThread.thread.start();
        QTRY_COMPARE(listening.count(), 1);
        TcpSender sender;
        QSignalSpy connected(&sender, &TcpSender::connected);
        QSignalSpy drained(&sender, &TcpSender::drained);
        sender.connectToHost("127.0.0.1", listening.at(0).at(0).value<quint16>());
        QTRY_COMPARE(connected.count(), 1);
        auto encoder = H264Encoder::create(config.frameSize);
        QVERIFY(encoder.has_value());
        for (int frame = 0; frame < 4; ++frame) {
            auto encoded = (*encoder)->encode(testFrame(config.frameSize, frame == 3 ? Qt::blue : Qt::red),
                                              frame * 1000 / 15);
            QVERIFY(encoded.has_value());
            QVERIFY(encoded->has_value());
            QVERIFY(sender.sendMessage(std::move(encoded->value())));
        }
        QTRY_VERIFY(drained.count() > 0);
        QTRY_COMPARE(images.count(), 1);
        QTRY_COMPARE(decoded.count(), 4);
        QCOMPARE(images.count(), 1); // No GUI acknowledgement yet.
        const quint64 ticket = images.at(0).at(1).value<quint64>();
        QMetaObject::invokeMethod(worker, [worker, ticket] {
            worker->acknowledgeImage(ticket);
        }, Qt::QueuedConnection);
        QTRY_COMPARE(images.count(), 2);
        const QImage latest = images.at(1).at(0).value<QImage>();
        QVERIFY(latest.pixelColor(80, 80).blue() > 220);
    }

    void backpressureSkipsBeforeEncodingAndKeepsSessionUsable()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        StreamConfig config;
        config.role = StreamRole::Sender;
        config.port = server.serverPort();
        config.frameSize = QSize(320, 240);
        WorkerThread workerThread(config);
        StreamWorker *worker = workerThread.worker;
        WorkerProbe probe;
        connect(worker, &StreamWorker::connectionChanged, &probe, &WorkerProbe::connectionChanged);
        connect(worker, &StreamWorker::frameProcessed, &probe, &WorkerProbe::frameProcessed);
        connect(worker, &StreamWorker::errorOccurred, &probe, &WorkerProbe::errorOccurred);
        QSignalSpy connections(&probe, &WorkerProbe::connectionChanged);
        QSignalSpy processed(&probe, &WorkerProbe::frameProcessed);
        QSignalSpy errors(&probe, &WorkerProbe::errorOccurred);
        workerThread.thread.start();
        QTRY_VERIFY(server.hasPendingConnections());
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QTRY_COMPARE(connections.count(), 1);
        QVERIFY(connections.at(0).at(0).toBool());
        QByteArray received;
        connect(peer, &QTcpSocket::readyRead, this, [&] { received += peer->readAll(); });
        QMetaObject::invokeMethod(worker, [worker, config] {
            auto *transport = worker->findChild<TcpSender *>();
            // The same worker event fills Qt's write buffer and attempts a
            // capture before the event loop can drain those pending bytes.
            transport->sendMessage(QByteArray(TransportLimits::maxPayloadBytes, 'x'));
            worker->processFrame(testFrame(config.frameSize, Qt::red));
        }, Qt::QueuedConnection);
        QTRY_COMPARE(processed.count(), 1);
        QVERIFY(!processed.at(0).at(0).toBool());
        QTRY_COMPARE(received.size(), qsizetype(TransportLimits::maxPayloadBytes) + 4);
        received.clear();
        QMetaObject::invokeMethod(worker, [worker, config] {
            worker->processFrame(testFrame(config.frameSize, Qt::blue));
        }, Qt::QueuedConnection);
        QTRY_COMPARE(processed.count(), 2);
        QVERIFY(processed.at(1).at(0).toBool());
        QTRY_VERIFY(received.size() >= 4);
        const quint32 length = qFromBigEndian<quint32>(received.constData());
        QTRY_COMPARE(received.size(), qsizetype(length) + 4);
        auto decoder = H264Decoder::create();
        QVERIFY(decoder.has_value());
        auto decoded = (*decoder)->decode(received.mid(4));
        QVERIFY(decoded.has_value());
        QVERIFY(decoded->has_value());
        // A fresh decoder can decode this first emitted video packet: the
        // skipped capture did not advance or damage reference-frame state.
        QVERIFY(decoded->value().pixelColor(80, 80).blue() > 220);
        QVERIFY(errors.isEmpty());
        QCOMPARE(connections.count(), 1);
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(oldHandler);
        QMutexLocker locker(&warningMutex);
        QVERIFY2(threadWarnings.isEmpty(), qPrintable(threadWarnings.join('\n')));
    }
};

QTEST_GUILESS_MAIN(StreamTests)
#include "stream_tests.moc"
