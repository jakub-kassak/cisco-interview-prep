#include "image_transfer_window.h"
#include "transfer_options.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

#include <optional>

namespace {
std::optional<TransferOptions> parseOptions(const QApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("Send one PNG over TCP and display it.");
    parser.addHelpOption();
    parser.addOptions({
        {{"m", "mode"}, "Process mode: send or receive.", "mode", "receive"},
        {{"H", "host"}, "Sender destination hostname or receiver bind IP.", "host", "127.0.0.1"},
        {{"p", "port"}, "TCP port (1–65535).", "port", "5000"},
        {{"f", "file"}, "PNG to send automatically when connected (send mode).", "path"},
    });
    parser.process(app);

    bool portOk = false;
    const uint port = parser.value("port").toUInt(&portOk);
    const QString mode = parser.value("mode");
    if (!portOk || port == 0 || port > 65535 ||
        (mode != "send" && mode != "receive")) {
        qCritical() << "Use --mode send|receive and --port 1–65535.";
        return std::nullopt;
    }

    TransferOptions options;
    options.mode = mode == "send" ? TransferMode::Send : TransferMode::Receive;
    options.host = parser.value("host");
    options.port = static_cast<quint16>(port);
    options.filePath = parser.value("file");
    return options;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("image-transfer-poc");

    const auto options = parseOptions(app);
    if (!options) {
        return 1;
    }

    ImageTransferWindow window(*options);
    if (!window.start()) {
        return 1;
    }
    window.show();
    return app.exec();
}
