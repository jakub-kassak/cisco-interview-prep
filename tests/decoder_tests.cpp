#include "h264_decoder.h"
#include "codec_test_images.h"
#include "h264_encoder.h"
#include "i420_image.h"

#include <QTest>

class DecoderTests : public QObject
{
    Q_OBJECT
private slots:
    void convertsPaddedI420Planes()
    {
        const QSize size(4, 2);
        const unsigned char y[] = {81, 81, 81, 81, 0, 0, 81, 81, 81, 81, 0, 0};
        const unsigned char u[] = {90, 90, 0};
        const unsigned char v[] = {240, 240, 0};
        auto image = imageFromI420(y, u, v, size, 6, 3);
        QVERIFY(image.has_value());
        QCOMPARE(image->size(), size);
        QVERIFY(image->pixelColor(3, 1).red() > 250);
        QVERIFY(image->pixelColor(3, 1).green() < 5);
        QVERIFY(image->pixelColor(3, 1).blue() < 5);
        QVERIFY(!imageFromI420(nullptr, u, v, size, 6, 3));
        QVERIFY(!imageFromI420(y, u, v, size, 3, 3));
    }

    void decodesMovingColorsAndOwnsPixels()
    {
        const QSize size(322, 242);
        auto encoder = H264Encoder::create(size);
        auto decoder = H264Decoder::create();
        QVERIFY(encoder.has_value());
        QVERIFY(decoder.has_value());
        QImage firstImage;
        QByteArray firstPixels;
        int decodedFrames = 0;
        for (int frame = 0; frame < 10; ++frame) {
            const QImage expected = CodecTestImages::colorFrame(size, frame);
            auto encoded = (*encoder)->encode(expected, frame * 1000 / 15);
            QVERIFY(encoded.has_value());
            if (!encoded->has_value()) {
                continue;
            }
            auto decoded = (*decoder)->decode(encoded->value());
            QVERIFY2(decoded.has_value(), qPrintable(decoded ? QString{} : decoded.error()));
            QVERIFY(decoded->has_value());
            const QImage &image = decoded->value();
            QCOMPARE(image.size(), size);
            QVERIFY(image.pixelColor(40, 40).red() > 220);
            QVERIFY(image.pixelColor(150, 40).green() > 220);
            QVERIFY(image.pixelColor(270, 40).blue() > 220);
            QVERIFY(CodecTestImages::meanColorError(expected, image) < 12);
            if (firstImage.isNull()) {
                firstImage = image;
                firstPixels = CodecTestImages::pixelBytes(image);
            }
            ++decodedFrames;
        }
        QVERIFY(decodedFrames >= 5);
        decoder->reset();
        QCOMPARE(CodecTestImages::pixelBytes(firstImage), firstPixels);
    }

    void rejectsEmptyAndMalformedAccessUnits()
    {
        auto decoder = H264Decoder::create();
        QVERIFY(decoder.has_value());
        QVERIFY(!(*decoder)->decode({}));
        const QByteArray invalid("\0\0\0\1\xFF\xFF\xFF\xFF", 8);
        QVERIFY(!(*decoder)->decode(invalid));
    }
};

QTEST_GUILESS_MAIN(DecoderTests)
#include "decoder_tests.moc"
