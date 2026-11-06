// Small process fixture for proving that the same TCP protocol works between
// independent event loops/processes, not only two workers in one test process.
#include "stream_session.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 2) {
        return 1;
    }
    const bool sending = arguments.at(1) == "send";
    StreamConfig config;
    config.frameSize = QSize(320, 240);
    config.role = sending ? StreamRole::Sender : StreamRole::Receiver;
    config.port = sending && arguments.size() == 3 ? arguments.at(2).toUShort() : 0;
    StreamSession session(config);
    bool wasConnected = false;
    bool finished = false;
    QObject::connect(&session, &StreamSession::listening, &app, [](quint16 port) {
        QTextStream(stdout) << "LISTENING " << port << Qt::endl;
    });
    QObject::connect(&session, &StreamSession::connectionChanged, &app, [&](bool connected) {
        if (!sending || finished) {
            return;
        }
        if (connected) {
            wasConnected = true;
            QImage image(config.frameSize, QImage::Format_RGB32);
            image.fill(Qt::blue);
            if (!session.submitFrame(std::move(image))) {
                app.exit(2);
            }
        } else if (wasConnected) {
            finished = true;
            session.stop();
            app.quit();
        }
    });
    QObject::connect(&session, &StreamSession::imageReady, &app, [&](const QImage &image) {
        if (sending || finished) {
            return;
        }
        finished = true;
        const bool valid = image.size() == config.frameSize &&
                           image.pixelColor(80, 80).blue() > 220;
        QTextStream(stdout) << (valid ? "FRAME_OK" : "FRAME_INVALID") << Qt::endl;
        session.stop();
        app.exit(valid ? 0 : 3);
    });
    QObject::connect(&session, &StreamSession::errorOccurred, &app, [&](const QString &error) {
        QTextStream(stderr) << error << Qt::endl;
        if (!sending) {
            app.exit(4);
        }
    });
    QTimer::singleShot(10000, &app, [&] { app.exit(5); });
    session.start();
    return app.exec();
}
