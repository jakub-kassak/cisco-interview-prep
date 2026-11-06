#include "h264_encoder.h"

#include "i420_image.h"

#include <QImage>
#include <wels/codec_api.h>

#include <limits>
#include <utility>

namespace {
std::expected<void, QString> validateConfiguration(
    QSize frameSize, int framesPerSecond, int bitrate)
{
    if (frameSize.width() < 16 || frameSize.height() < 16 ||
        frameSize.width() % 2 != 0 || frameSize.height() % 2 != 0 ||
        qint64(frameSize.width()) * frameSize.height() >
            qint64(std::numeric_limits<int>::max()) * 2 / 3) {
        return std::unexpected(QStringLiteral("Encoder dimensions must be even, at least 16x16, and fit the codec."));
    }
    if (framesPerSecond < 1 || framesPerSecond > 60 || bitrate < 1) {
        return std::unexpected(QStringLiteral("Encoder requires 1–60 fps and a positive bitrate."));
    }
    return {};
}

std::expected<SEncParamExt, QString> makeParameters(
    ISVCEncoder &encoder, QSize frameSize, int framesPerSecond, int bitrate)
{
    SEncParamExt parameters{};
    if (encoder.GetDefaultParams(&parameters) != 0) {
        return std::unexpected(QStringLiteral("Could not get OpenH264 defaults."));
    }
    parameters.iUsageType = SCREEN_CONTENT_REAL_TIME;
    parameters.iPicWidth = frameSize.width();
    parameters.iPicHeight = frameSize.height();
    parameters.fMaxFrameRate = float(framesPerSecond);
    parameters.iTargetBitrate = bitrate;
    parameters.iRCMode = RC_BITRATE_MODE;
    parameters.bEnableFrameSkip = true;
    parameters.bEnableAdaptiveQuant = false;
    parameters.bEnableBackgroundDetection = false;
    parameters.iSpatialLayerNum = 1;
    parameters.iTemporalLayerNum = 1;
    parameters.iMultipleThreadIdc = 1;
    parameters.uiIntraPeriod = unsigned(framesPerSecond * 2);
    auto &layer = parameters.sSpatialLayers[0];
    layer.iVideoWidth = frameSize.width();
    layer.iVideoHeight = frameSize.height();
    layer.fFrameRate = float(framesPerSecond);
    layer.iSpatialBitrate = bitrate;
    // Describe the same range and color matrix used by imageToI420().
    layer.bVideoSignalTypePresent = true;
    layer.uiVideoFormat = VF_UNDEF;
    layer.bFullRange = false;
    layer.bColorDescriptionPresent = true;
    layer.uiColorPrimaries = CP_UNDEF;
    layer.uiTransferCharacteristics = TRC_UNDEF;
    layer.uiColorMatrix = CM_SMPTE170M;
    return parameters;
}

SSourcePicture makeSourcePicture(QByteArray &pixels, QSize size, qint64 timestampMs)
{
    const int width = size.width();
    const int height = size.height();
    const qsizetype ySize = qsizetype(width) * height;
    auto *data = reinterpret_cast<unsigned char *>(pixels.data());
    SSourcePicture picture{};
    picture.iColorFormat = videoFormatI420;
    picture.iPicWidth = width;
    picture.iPicHeight = height;
    picture.iStride[0] = width;
    picture.iStride[1] = picture.iStride[2] = width / 2;
    picture.pData[0] = data;
    picture.pData[1] = data + ySize;
    picture.pData[2] = data + ySize + ySize / 4;
    picture.uiTimeStamp = timestampMs;

    return picture;
}

QByteArray copyAccessUnit(const SFrameBSInfo &encoded)
{
    // OpenH264 owns these buffers and reuses them on the next encode call.
    // Copy all NALs from all layers into an independently owned access unit.
    QByteArray bytes;
    for (int layerIndex = 0; layerIndex < encoded.iLayerNum; ++layerIndex) {
        const auto &layer = encoded.sLayerInfo[layerIndex];
        qsizetype layerBytes = 0;
        for (int nal = 0; nal < layer.iNalCount; ++nal) {
            layerBytes += layer.pNalLengthInByte[nal];
        }
        bytes.append(reinterpret_cast<const char *>(layer.pBsBuf), layerBytes);
    }
    return bytes;
}
}

void H264Encoder::EncoderDeleter::operator()(ISVCEncoder *encoder) const
{
    encoder->Uninitialize();
    WelsDestroySVCEncoder(encoder);
}

H264Encoder::H264Encoder(QSize frameSize)
    : frameSize_(frameSize)
{
}

H264Encoder::~H264Encoder() = default;

std::expected<std::unique_ptr<H264Encoder>, QString> H264Encoder::create(
    QSize frameSize, int framesPerSecond, int bitrate)
{
    const auto validation = validateConfiguration(frameSize, framesPerSecond, bitrate);
    if (!validation) {
        return std::unexpected(validation.error());
    }

    auto result = std::unique_ptr<H264Encoder>(new H264Encoder(frameSize));
    ISVCEncoder *rawEncoder = nullptr;
    if (WelsCreateSVCEncoder(&rawEncoder) != 0 || !rawEncoder) {
        return std::unexpected(QStringLiteral("Could not create OpenH264 encoder."));
    }
    result->encoder_.reset(rawEncoder);

    auto parameters = makeParameters(*rawEncoder, frameSize, framesPerSecond, bitrate);
    if (!parameters) {
        return std::unexpected(parameters.error());
    }
    if (rawEncoder->InitializeExt(&parameters.value()) != 0) {
        return std::unexpected(QStringLiteral("Could not initialize OpenH264 encoder."));
    }
    // A new session starts with an IDR and its parameter sets, so a fresh
    // decoder has all the information it needs without earlier frames.
    if (rawEncoder->ForceIntraFrame(true) != 0) {
        return std::unexpected(QStringLiteral("Could not request the initial IDR frame."));
    }
    return result;
}

std::expected<std::optional<QByteArray>, QString> H264Encoder::encode(
    const QImage &image, qint64 timestampMs)
{
    if (image.isNull() || image.size() != frameSize_) {
        return std::unexpected(QStringLiteral("Image dimensions do not match the encoder."));
    }
    if (timestampMs < 0 || timestampMs < lastTimestampMs_) {
        return std::unexpected(QStringLiteral("Timestamps must be nonnegative and nondecreasing."));
    }
    auto pixels = imageToI420(image);
    if (!pixels) {
        return std::unexpected(pixels.error());
    }

    auto picture = makeSourcePicture(pixels.value(), frameSize_, timestampMs);

    SFrameBSInfo encoded{};
    if (encoder_->EncodeFrame(&picture, &encoded) != 0) {
        return std::unexpected(QStringLiteral("OpenH264 encoding failed."));
    }
    lastTimestampMs_ = timestampMs;
    if (encoded.eFrameType == videoFrameTypeSkip) {
        return std::nullopt;
    }

    QByteArray bytes = copyAccessUnit(encoded);
    if (bytes.isEmpty()) {
        return std::unexpected(QStringLiteral("OpenH264 returned an empty access unit."));
    }
    return std::optional<QByteArray>{std::move(bytes)};
}
