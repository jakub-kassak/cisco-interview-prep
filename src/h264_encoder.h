#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>

#include <expected>
#include <memory>
#include <optional>

class ISVCEncoder;
class QImage;

// One encoder represents one ordered video session. Use it from one thread;
// create a fresh encoder for a new receiver connection.
class H264Encoder
{
public:
    static std::expected<std::unique_ptr<H264Encoder>, QString> create(
        QSize frameSize, int framesPerSecond = 15, int bitrate = 1'000'000);

    // Input must match the configured size. Timestamps are milliseconds,
    // nonnegative and nondecreasing. Alpha is discarded.
    // Success contains owned Annex B bytes for one access unit, including all
    // parameter-set NALs emitted by OpenH264. nullopt means the codec skipped it.
    std::expected<std::optional<QByteArray>, QString> encode(
        const QImage &image, qint64 timestampMs);

    ~H264Encoder();
    H264Encoder(const H264Encoder &) = delete;
    H264Encoder &operator=(const H264Encoder &) = delete;

private:
    struct EncoderDeleter {
        void operator()(ISVCEncoder *encoder) const;
    };

    explicit H264Encoder(QSize frameSize);

    const QSize frameSize_;
    std::unique_ptr<ISVCEncoder, EncoderDeleter> encoder_;
    qint64 lastTimestampMs_ = -1;
};
