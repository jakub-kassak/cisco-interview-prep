#pragma once

#include "stream_config.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>

#include <expected>
#include <memory>
#include <optional>

class H264Decoder;
class H264Encoder;
class TcpReceiver;
class TcpSender;

// Lives entirely on the session's worker thread. No QWidget access here.
class StreamWorker : public QObject
{
    Q_OBJECT
public:
    explicit StreamWorker(StreamConfig config);
    ~StreamWorker() override;
    void start();
    void stop();
    void processFrame(QImage image);
    void acknowledgeImage(quint64 ticket);

signals:
    void statusChanged(QString message);
    void errorOccurred(QString message);
    void connectionChanged(bool connected);
    void listening(quint16 port);
    void frameProcessed(bool sent);
    void imageReady(QImage image, quint64 ticket);
    void frameDecoded();

private:
    void startSender();
    void startReceiver();
    void receiveMessage(const QByteArray &bytes);
    std::expected<bool, QString> encodeAndSend(const QImage &image);
    void publishImage(QImage image);
    void resetDisplay();

    const StreamConfig config_;
    TcpSender *sender_ = nullptr;
    TcpReceiver *receiver_ = nullptr;
    std::unique_ptr<H264Encoder> encoder_;
    std::unique_ptr<H264Decoder> decoder_;
    QElapsedTimer clock_;
    bool connected_ = false;
    std::optional<QImage> latestImage_;
    quint64 nextImageTicket_ = 0;
    quint64 awaitingImageTicket_ = 0;
};
