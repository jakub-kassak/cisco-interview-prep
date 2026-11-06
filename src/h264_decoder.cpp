#include "h264_decoder.h"

#include "i420_image.h"

#include <wels/codec_api.h>

#include <limits>
#include <utility>

void H264Decoder::DecoderDeleter::operator()(ISVCDecoder *decoder) const
{
    decoder->Uninitialize();
    WelsDestroyDecoder(decoder);
}

H264Decoder::~H264Decoder() = default;

std::expected<std::unique_ptr<H264Decoder>, QString> H264Decoder::create()
{
    auto result = std::unique_ptr<H264Decoder>(new H264Decoder);
    ISVCDecoder *rawDecoder = nullptr;
    if (WelsCreateDecoder(&rawDecoder) != 0 || !rawDecoder) {
        return std::unexpected(QStringLiteral("Could not create OpenH264 decoder."));
    }
    result->decoder_.reset(rawDecoder);
    SDecodingParam parameters{};
    parameters.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    parameters.eEcActiveIdc = ERROR_CON_DISABLE;
    if (rawDecoder->Initialize(&parameters) != 0) {
        return std::unexpected(QStringLiteral("Could not initialize OpenH264 decoder."));
    }
    return result;
}

std::expected<std::optional<QImage>, QString> H264Decoder::decode(const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > std::numeric_limits<int>::max()) {
        return std::unexpected(QStringLiteral("Encoded access unit has an invalid size."));
    }
    unsigned char *planes[3]{};
    SBufferInfo info{};
    const auto state = decoder_->DecodeFrameNoDelay(
        reinterpret_cast<const unsigned char *>(bytes.constData()),
        int(bytes.size()), planes, &info);
    if (state != dsErrorFree) {
        return std::unexpected(QStringLiteral("OpenH264 could not decode the access unit (%1).")
                                   .arg(int(state)));
    }
    if (info.iBufferStatus == 0) {
        return std::nullopt;
    }
    const auto &system = info.UsrData.sSystemBuffer;
    // Keep RGB allocation bounded as well as encoded TCP payload size.
    if (system.iWidth < 1 || system.iHeight < 1 ||
        system.iWidth > 4096 || system.iHeight > 4096) {
        return std::unexpected(QStringLiteral("Decoded picture exceeds the supported dimensions."));
    }
    auto image = imageFromI420(planes[0], planes[1], planes[2],
                              QSize(system.iWidth, system.iHeight),
                              system.iStride[0], system.iStride[1]);
    if (!image) {
        return std::unexpected(image.error());
    }
    return std::optional<QImage>{std::move(image.value())};
}
