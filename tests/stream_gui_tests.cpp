#include "paint_stream_window.h"
#include "stream_session.h"

#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

class StreamGuiTests : public QObject
{
    Q_OBJECT
private slots:
    void paintsStreamsClearsAndReconnects()
    {
        StreamConfig receiverConfig;
        receiverConfig.port = 0;
        PaintStreamWindow receiver(receiverConfig);
        auto *receiveSession = receiver.findChild<StreamSession *>();
        QVERIFY(receiveSession);
        QSignalSpy listening(receiveSession, &StreamSession::listening);
        QSignalSpy images(receiveSession, &StreamSession::imageReady);
        QSignalSpy receiveErrors(receiveSession, &StreamSession::errorOccurred);
        receiver.show();
        receiver.start();
        QTRY_COMPARE(listening.count(), 1);

        StreamConfig senderConfig;
        senderConfig.role = StreamRole::Sender;
        senderConfig.port = listening.at(0).at(0).value<quint16>();
        PaintStreamWindow sender(senderConfig);
        auto *sendSession = sender.findChild<StreamSession *>();
        auto *canvas = sender.findChild<QWidget *>("paintCanvas");
        auto *clearButton = sender.findChild<QPushButton *>("clearButton");
        auto *connectionButton = sender.findChild<QPushButton *>("connectionButton");
        QVERIFY(sendSession && canvas && clearButton && connectionButton);
        QSignalSpy sendErrors(sendSession, &StreamSession::errorOccurred);
        sender.show();
        QTest::mousePress(canvas, Qt::LeftButton, {}, QPoint(100, 100));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, QPoint(200, 100));
        int heartbeatCount = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeatCount; });
        heartbeat.start(5);
        sender.start();
        QTRY_VERIFY(images.count() >= 2);
        QVERIFY(heartbeatCount > 1);
        QImage image = images.last().at(0).value<QImage>();
        QCOMPARE(image.size(), QSize(960, 540));
        QVERIFY(image.pixelColor(150, 100).red() < 50);
        QVERIFY(image.pixelColor(400, 400).red() > 220);
        auto *videoView = receiver.findChild<QWidget *>("videoView");
        QVERIFY(videoView);
        QVERIFY(videoView->grab().save("received-stream-preview.png"));

        QTest::mouseClick(clearButton, Qt::LeftButton);
        QTRY_VERIFY(images.last().at(0).value<QImage>().pixelColor(150, 100).red() > 220);
        QTest::mouseClick(connectionButton, Qt::LeftButton);
        QVERIFY(!sendSession->isRunning());
        QTRY_VERIFY(!receiveSession->isConnected());
        QTest::mouseClick(connectionButton, Qt::LeftButton);
        QTRY_VERIFY(sendSession->isConnected());
        const qsizetype beforeReconnectFrame = images.count();
        QTRY_VERIFY(images.count() > beforeReconnectFrame);
        QVERIFY(images.last().at(0).value<QImage>().pixelColor(150, 100).red() > 220);
        QVERIFY(sendErrors.isEmpty());
        QVERIFY(receiveErrors.isEmpty());
        // Both windows shut down connected workers in their destructors.
    }
};

QTEST_MAIN(StreamGuiTests)
#include "stream_gui_tests.moc"
