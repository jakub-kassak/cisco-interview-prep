#include <wels/codec_api.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
constexpr int width = 320;
constexpr int height = 240;
constexpr int frameCount = 30;
constexpr int lumaSize = width * height;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct EncoderDeleter {
    void operator()(ISVCEncoder* encoder) const {
        encoder->Uninitialize();
        WelsDestroySVCEncoder(encoder);
    }
};
struct DecoderDeleter {
    void operator()(ISVCDecoder* decoder) const {
        decoder->Uninitialize();
        WelsDestroyDecoder(decoder);
    }
};
using Encoder = std::unique_ptr<ISVCEncoder, EncoderDeleter>;
using Decoder = std::unique_ptr<ISVCDecoder, DecoderDeleter>;

Encoder createEncoder()
{
    ISVCEncoder* rawEncoder = nullptr;
    require(WelsCreateSVCEncoder(&rawEncoder) == 0 && rawEncoder,
            "Cannot create encoder");
    Encoder encoder(rawEncoder);
    SEncParamExt parameters{};
    require(encoder->GetDefaultParams(&parameters) == 0, "Cannot get defaults");
    parameters.iUsageType = SCREEN_CONTENT_REAL_TIME;
    parameters.iPicWidth = width;
    parameters.iPicHeight = height;
    parameters.fMaxFrameRate = 30.0f;
    parameters.iTargetBitrate = 500000;
    // Fixed quantization keeps this smoke test deterministic, without skips.
    parameters.iRCMode = RC_OFF_MODE;
    parameters.bEnableFrameSkip = false;
    parameters.bEnableAdaptiveQuant = false;
    parameters.bEnableBackgroundDetection = false;
    parameters.iSpatialLayerNum = 1;
    parameters.iTemporalLayerNum = 1;
    parameters.sSpatialLayers[0].iVideoWidth = width;
    parameters.sSpatialLayers[0].iVideoHeight = height;
    parameters.sSpatialLayers[0].fFrameRate = 30.0f;
    parameters.sSpatialLayers[0].iSpatialBitrate = parameters.iTargetBitrate;
    require(encoder->InitializeExt(&parameters) == 0, "Cannot initialize encoder");
    return encoder;
}

Decoder createDecoder()
{
    ISVCDecoder* rawDecoder = nullptr;
    require(WelsCreateDecoder(&rawDecoder) == 0 && rawDecoder,
            "Cannot create decoder");
    Decoder decoder(rawDecoder);
    SDecodingParam decoding{};
    decoding.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    require(decoder->Initialize(&decoding) == 0, "Cannot initialize decoder");
    return decoder;
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc <= 2, "Usage: codec_smoke [output.h264]");
        std::ofstream output;
        if (argc == 2) {
            output.open(argv[1], std::ios::binary);
            require(output.is_open(), "Cannot open output file");
        }

        auto encoder = createEncoder();
        auto decoder = createDecoder();

        // I420 is three planes: full-size Y, then quarter-size U and V.
        std::vector<unsigned char> pixels(lumaSize * 3 / 2, 128);
        SSourcePicture picture{};
        picture.iColorFormat = videoFormatI420;
        picture.iPicWidth = width;
        picture.iPicHeight = height;
        picture.iStride[0] = width;
        picture.iStride[1] = picture.iStride[2] = width / 2;
        picture.pData[0] = pixels.data();
        picture.pData[1] = pixels.data() + lumaSize;
        picture.pData[2] = pixels.data() + lumaSize + lumaSize / 4;

        std::size_t totalBytes = 0;
        double totalError = 0;
        int decodedFrames = 0;
        for (int frame = 0; frame < frameCount; ++frame) {
            // Moving dark square on a light canvas, with neutral chroma.
            std::fill_n(pixels.begin(), lumaSize, 220);
            for (int y = 80; y < 120; ++y)
                for (int x = frame * 4; x < frame * 4 + 40; ++x)
                    pixels[y * width + x] = 30;
            picture.uiTimeStamp = frame * 1000 / 30;
            SFrameBSInfo encoded{};
            require(encoder->EncodeFrame(&picture, &encoded) == 0, "Encoding failed");
            require(encoded.eFrameType != videoFrameTypeSkip, "Unexpected skipped frame");

            // Copy every layer's NAL units before the next EncodeFrame call.
            std::vector<unsigned char> packet;
            for (int layer = 0; layer < encoded.iLayerNum; ++layer) {
                const auto& info = encoded.sLayerInfo[layer];
                int bytes = 0;
                for (int nal = 0; nal < info.iNalCount; ++nal)
                    bytes += info.pNalLengthInByte[nal];
                packet.insert(packet.end(), info.pBsBuf, info.pBsBuf + bytes);
            }
            require(!packet.empty(), "Encoder returned no data");
            totalBytes += packet.size();
            if (output.is_open()) {
                output.write(reinterpret_cast<const char*>(packet.data()), packet.size());
                require(output.good(), "Cannot write output");
            }

            unsigned char* planes[3]{};
            SBufferInfo decoded{};
            require(decoder->DecodeFrameNoDelay(packet.data(), static_cast<int>(packet.size()),
                        planes, &decoded) == dsErrorFree, "Decoding failed");
            require(decoded.iBufferStatus == 1, "Decoder produced no frame");
            const auto& system = decoded.UsrData.sSystemBuffer;
            require(system.iWidth == width && system.iHeight == height,
                    "Decoded dimensions differ");
            // H.264 is lossy: compare with tolerance and respect decoder stride.
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    totalError += std::abs(int(pixels[y * width + x]) -
                                          int(planes[0][y * system.iStride[0] + x]));
            ++decodedFrames;
        }
        const double meanError = totalError / (decodedFrames * lumaSize);
        require(decodedFrames == frameCount && meanError < 10.0,
                "Roundtrip quality check failed");
        if (output.is_open()) {
            output.close();
            require(!output.fail(), "Cannot finish output file");
        }
        const auto version = WelsGetCodecVersion();
        std::cout << "OpenH264 " << version.uMajor << '.' << version.uMinor << '.'
                  << version.uRevision << ": " << decodedFrames << " frames, "
                  << width << 'x' << height << ", " << totalBytes
                  << " encoded bytes, mean luma error " << meanError << "\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
