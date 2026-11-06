#include "stream_worker.h"

#include "h264_decoder.h"
#include "h264_encoder.h"
#include "tcp_receiver.h"
#include "tcp_sender.h"
#include "transport_limits.h"

#include <QThread>

#include <utility>

StreamWorker::StreamWorker(StreamConfig config)
    : config_(std::move(config))
{
}

StreamWorker::~StreamWorker()
{
    stop();
}

void StreamWorker::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (config_.role == StreamRole::Sender) {
        startSender();
    } else {
        startReceiver();
    }
}

void StreamWorker::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    connected_ = false;
    if (sender_) {
        sender_->disconnect(this);
        sender_->disconnectFromHost();
        delete sender_;
        sender_ = nullptr;
    }
    if (receiver_) {
        receiver_->disconnect(this);
        receiver_->close();
        delete receiver_;
        receiver_ = nullptr;
    }
    encoder_.reset();
    decoder_.reset();
    resetDisplay();
}

void StreamWorker::startSender()
{
    sender_ = new TcpSender(this);
    connect(sender_, &TcpSender::connected, this, [this] {
        auto encoder = H264Encoder::create(
            config_.frameSize, config_.framesPerSecond, config_.bitrate);
        if (!encoder) {
            emit errorOccurred(encoder.error());
            sender_->disconnectFromHost();
            return;
        }
        encoder_ = std::move(encoder.value());
        clock_.start();
        connected_ = true;
        emit connectionChanged(true);
        emit statusChanged(QStringLiteral("Connected; streaming the canvas."));
    });
    connect(sender_, &TcpSender::disconnected, this, [this] {
        connected_ = false;
        encoder_.reset();
        emit connectionChanged(false);
        emit statusChanged(QStringLiteral("Disconnected. Use Connect to start a fresh session."));
    });
    connect(sender_, &TcpSender::errorOccurred, this, &StreamWorker::errorOccurred);
    emit statusChanged(QStringLiteral("Connecting…"));
    sender_->connectToHost(config_.host, config_.port);
}

void StreamWorker::startReceiver()
{
    receiver_ = new TcpReceiver(this);
    connect(receiver_, &TcpReceiver::connected, this, [this] {
        auto decoder = H264Decoder::create();
        if (!decoder) {
            emit errorOccurred(decoder.error());
            receiver_->disconnectPeer();
            return;
        }
        decoder_ = std::move(decoder.value());
        resetDisplay();
        connected_ = true;
        emit connectionChanged(true);
        emit statusChanged(QStringLiteral("Sender connected; waiting for video."));
    });
    connect(receiver_, &TcpReceiver::disconnected, this, [this] {
        connected_ = false;
        decoder_.reset();
        resetDisplay();
        emit connectionChanged(false);
        emit statusChanged(QStringLiteral("Sender disconnected; listening for another sender."));
    });
    connect(receiver_, &TcpReceiver::errorOccurred, this, &StreamWorker::errorOccurred);
    connect(receiver_, &TcpReceiver::messageReceived,
            this, &StreamWorker::receiveMessage);
    const QHostAddress address(config_.host);
    if (address.isNull() || !receiver_->listen(address, config_.port)) {
        emit errorOccurred(QStringLiteral("Could not listen on %1:%2.")
                               .arg(config_.host).arg(config_.port));
        return;
    }
    emit listening(receiver_->serverPort());
    emit statusChanged(QStringLiteral("Listening on %1:%2")
                           .arg(config_.host).arg(receiver_->serverPort()));
}

std::expected<bool, QString> StreamWorker::encodeAndSend(const QImage &image)
{
    if (!connected_ || !encoder_) {
        return false;
    }
    // Reserve the largest permitted access unit before touching the encoder.
    // A skipped raw capture has no decoder dependencies. A dropped encoded
    // reference picture would corrupt later pictures, so never discard one
    // and continue the same session after it fails to enter the TCP buffer.
    if (!sender_->canAcceptMessage(TransportLimits::maxPayloadBytes)) {
        return false;
    }
    auto encoded = encoder_->encode(image, clock_.elapsed());
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    if (!encoded->has_value()) {
        return false; // Codec-managed frame skip.
    }
    if (!sender_->sendMessage(std::move(encoded->value()))) {
        return std::unexpected(QStringLiteral("Encoded frame could not be sent; ending the session."));
    }
    return true;
}

void StreamWorker::processFrame(QImage image)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto result = encodeAndSend(image);
    if (!result) {
        emit errorOccurred(result.error());
        if (sender_) {
            sender_->disconnectFromHost();
        }
    }
    emit frameProcessed(result && result.value());
}

void StreamWorker::receiveMessage(const QByteArray &bytes)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!decoder_) {
        return;
    }
    auto decoded = decoder_->decode(bytes);
    if (!decoded || (decoded->has_value() && decoded->value().size() != config_.frameSize)) {
        const QString error = decoded
            ? QStringLiteral("Decoded dimensions do not match this session.")
            : decoded.error();
        emit errorOccurred(error);
        receiver_->disconnectPeer();
        return;
    }
    if (decoded->has_value()) {
        emit frameDecoded();
        publishImage(std::move(decoded->value()));
    }
}

void StreamWorker::publishImage(QImage image)
{
    if (awaitingImageTicket_ != 0) {
        latestImage_ = std::move(image); // Replace an older decoded image.
        return;
    }
    awaitingImageTicket_ = ++nextImageTicket_;
    emit imageReady(std::move(image), awaitingImageTicket_);
}

void StreamWorker::acknowledgeImage(quint64 ticket)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (ticket != awaitingImageTicket_) {
        return; // A stale acknowledgement from an earlier connection.
    }
    awaitingImageTicket_ = 0;
    if (latestImage_) {
        QImage image = std::move(latestImage_.value());
        latestImage_.reset();
        publishImage(std::move(image));
    }
}

void StreamWorker::resetDisplay()
{
    latestImage_.reset();
    awaitingImageTicket_ = 0;
}
