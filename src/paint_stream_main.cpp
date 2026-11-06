#include "paint_stream_window.h"
#include "stream_config.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QHostAddress>

#include <optional>

namespace {
std::optional<StreamConfig> parseOptions(const QApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("Draw a bitmap and stream it with Cisco OpenH264 over TCP.");
    parser.addHelpOption();
    parser.addOptions({
        {{"m", "mode"}, "Process mode: send or receive.", "mode", "receive"},
        {{"H", "host"}, "Sender destination hostname or receiver bind IP.", "host", "127.0.0.1"},
        {{"p", "port"}, "TCP port (1–65535).", "port", "5000"},
    });
    parser.process(app);
    bool validPort = false;
    const uint port = parser.value("port").toUInt(&validPort);
    const QString mode = parser.value("mode");
    if (!validPort || port == 0 || port > 65535 ||
        (mode != "send" && mode != "receive") || parser.value("host").isEmpty()) {
        qCritical() << "Use --mode send|receive, --host HOST, and --port 1–65535.";
        return std::nullopt;
    }
    StreamConfig config;
    config.role = mode == "send" ? StreamRole::Sender : StreamRole::Receiver;
    config.host = parser.value("host");
    config.port = static_cast<quint16>(port);
    if (config.role == StreamRole::Receiver && QHostAddress(config.host).isNull()) {
        qCritical() << "Receiver --host must be a bind IP address, such as 127.0.0.1 or 0.0.0.0.";
        return std::nullopt;
    }
    return config;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("paint-stream");
    const auto config = parseOptions(app);
    if (!config) {
        return 1;
    }
    PaintStreamWindow window(*config);
    window.start();
    window.show();
    return app.exec();
}
