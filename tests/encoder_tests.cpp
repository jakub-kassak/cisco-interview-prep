#include "h264_encoder.h"
#include "codec_test_images.h"

#include <QImage>
#include <QTest>
#include <wels/codec_api.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
struct DecoderDeleter {
    void operator()(ISVCDecoder *decoder) const
    {
        decoder->Uninitialize();
        WelsDestroyDecoder(decoder);
    }
};
using Decoder = std::unique_ptr<ISVCDecoder, DecoderDeleter>;

std::expected<Decoder, QString> createDecoder()
{
    ISVCDecoder *raw = nullptr;
    if (WelsCreateDecoder(&raw) != 0 || !raw) {
        return std::unexpected(QStringLiteral("Could not create test decoder."));
    }
    Decoder decoder(raw);
    SDecodingParam parameters{};
    parameters.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    if (decoder->Initialize(&parameters) != 0) {
        return std::unexpected(QStringLiteral("Could not initialize test decoder."));
    }
    return decoder;
}

std::expected<QImage, QString> decode(ISVCDecoder &decoder, const QByteArray &bytes)
{
    unsigned char *planes[3]{};
    SBufferInfo info{};
    if (decoder.DecodeFrameNoDelay(reinterpret_cast<const unsigned char *>(bytes.constData()),
                                  int(bytes.size()), planes, &info) != dsErrorFree ||
        info.iBufferStatus != 1) {
        return std::unexpected(QStringLiteral("Test decoder did not produce a complete frame."));
    }
    const auto &system = info.UsrData.sSystemBuffer;
    QImage image(system.iWidth, system.iHeight, QImage::Format_RGB888);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const double luma = planes[0][y * system.iStride[0] + x] - 16;
            const double u = planes[1][(y / 2) * system.iStride[1] + x / 2] - 128;
            const double v = planes[2][(y / 2) * system.iStride[1] + x / 2] - 128;
            // Independently reconstruct BT.601 limited-range RGB for checking
            // visual colors, not exact equality of lossy encoded samples.
            row[x * 3] = std::clamp(int(std::lround(1.164 * luma + 1.596 * v)), 0, 255);
            row[x * 3 + 1] = std::clamp(int(std::lround(1.164 * luma - 0.392 * u - 0.813 * v)), 0, 255);
            row[x * 3 + 2] = std::clamp(int(std::lround(1.164 * luma + 2.017 * u)), 0, 255);
        }
    }
    return image;
}

bool containsNal(const QByteArray &bytes, int type)
{
    for (qsizetype i = 0; i + 3 < bytes.size(); ++i) {
        if (bytes[i] == '\0' && bytes[i + 1] == '\0' && bytes[i + 2] == '\1' &&
            (static_cast<unsigned char>(bytes[i + 3]) & 31) == type) {
            return true;
        }
    }
    return false;
}
}

class EncoderTests : public QObject
{
    Q_OBJECT
private slots:
    void rejectsInvalidConfigurationAndInput()
    {
        QVERIFY(!H264Encoder::create(QSize(0, 240)));
        QVERIFY(!H264Encoder::create(QSize(321, 240)));
        QVERIFY(!H264Encoder::create(QSize(320, 240), 0));
        QVERIFY(!H264Encoder::create(QSize(320, 240), 15, 0));
        auto encoder = H264Encoder::create(QSize(320, 240));
        QVERIFY(encoder.has_value());
        QVERIFY(!(*encoder)->encode(QImage{}, 0));
        QVERIFY(!(*encoder)->encode(QImage(322, 240, QImage::Format_RGB32), 0));
        const QImage frame = CodecTestImages::colorFrame(QSize(320, 240), 0, QImage::Format_RGB888);
        QVERIFY(!(*encoder)->encode(frame, -1));
        QVERIFY((*encoder)->encode(frame, 100).has_value());
        QVERIFY(!(*encoder)->encode(frame, 99));
        QVERIFY((*encoder)->encode(frame, 200).has_value());
    }

    void encodesColorsMotionAndOwnedOutput()
    {
        const QSize size(322, 242);
        auto created = H264Encoder::create(size);
        QVERIFY2(created.has_value(), qPrintable(created ? QString{} : created.error()));
        auto encoder = std::move(created.value());
        auto decoder = createDecoder();
        QVERIFY(decoder.has_value());
        QByteArray firstPacket;
        QByteArray firstPacketHex;
        int decodedCount = 0;

        for (int frame = 0; frame < 12; ++frame) {
            QImage image = CodecTestImages::colorFrame(size, frame, QImage::Format_RGB888);
            // Also exercise conversion from Qt's platform-native 32-bit layout.
            if (frame % 2 != 0) {
                image = image.convertToFormat(QImage::Format_RGB32);
            }
            auto encoded = encoder->encode(image, frame * 1000 / 15);
            QVERIFY2(encoded.has_value(), qPrintable(encoded ? QString{} : encoded.error()));
            if (!encoded->has_value()) {
                QVERIFY(frame != 0);
                continue;
            }
            if (frame == 0) {
                firstPacket = encoded->value();
                firstPacketHex = firstPacket.toHex();
                QVERIFY(containsNal(firstPacket, 7)); // SPS.
                QVERIFY(containsNal(firstPacket, 8)); // PPS.
                QVERIFY(containsNal(firstPacket, 5)); // IDR.
            }
            auto decoded = decode(**decoder, encoded->value());
            QVERIFY2(decoded.has_value(), qPrintable(decoded ? QString{} : decoded.error()));
            QCOMPARE(decoded->size(), size);
            QVERIFY(CodecTestImages::meanColorError(image, *decoded) < 12.0);
            // These checks catch swapped RGB or U/V channels specifically.
            QVERIFY(decoded->pixelColor(40, 40).red() > 220);
            QVERIFY(decoded->pixelColor(150, 40).green() > 220);
            QVERIFY(decoded->pixelColor(270, 40).blue() > 220);
            ++decodedCount;
        }
        QVERIFY(decodedCount >= 6);
        encoder.reset();
        QCOMPARE(firstPacket.toHex(), firstPacketHex);
        auto freshDecoder = createDecoder();
        QVERIFY(freshDecoder.has_value());
        auto firstImage = decode(**freshDecoder, firstPacket);
        QVERIFY(firstImage.has_value());
        QCOMPARE(firstImage->size(), size);
    }
};

QTEST_GUILESS_MAIN(EncoderTests)
#include "encoder_tests.moc"
