#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <expected>
#include <memory>
#include <optional>

class ISVCDecoder;

// One decoder per connection, used only on its owning worker thread.
class H264Decoder
{
public:
    static std::expected<std::unique_ptr<H264Decoder>, QString> create();
    // A complete Annex B access unit. nullopt means no picture was produced yet.
    // Returned QImages own their pixels independently of OpenH264's next call.
    std::expected<std::optional<QImage>, QString> decode(const QByteArray &bytes);
    ~H264Decoder();
    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

private:
    H264Decoder() = default;
    struct DecoderDeleter {
        void operator()(ISVCDecoder *decoder) const;
    };
    std::unique_ptr<ISVCDecoder, DecoderDeleter> decoder_;
};
