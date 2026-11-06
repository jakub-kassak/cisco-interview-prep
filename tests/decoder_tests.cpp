#include "h264_decoder.h"
#include "h264_encoder.h"
#include "i420_image.h"

#include <QPainter>
#include <QTest>

namespace {
QImage colorFrame(QSize size, int frame)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.fillRect(0, 0, size.width() / 3, size.height() / 2, Qt::red);
    painter.fillRect(size.width() / 3, 0, size.width() / 3, size.height() / 2, Qt::green);
    painter.fillRect(2 * size.width() / 3, 0, size.width() - 2 * size.width() / 3,
                     size.height() / 2, Qt::blue);
    painter.fillRect(frame * 10, size.height() - 30, 20, 20, Qt::black);
    return image;
}
}

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
            const QImage expected = colorFrame(size, frame);
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
            double error = 0;
            for (int y = 0; y < size.height(); ++y) {
                for (int x = 0; x < size.width(); ++x) {
                    const QColor a = image.pixelColor(x, y);
                    const QColor b = expected.pixelColor(x, y);
                    error += qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) +
                             qAbs(a.blue() - b.blue());
                }
            }
            QVERIFY(error / (size.width() * size.height() * 3) < 12);
            if (firstImage.isNull()) {
                firstImage = image;
                firstPixels = QByteArray(reinterpret_cast<const char *>(image.constBits()),
                                        image.sizeInBytes());
            }
            ++decodedFrames;
        }
        QVERIFY(decodedFrames >= 5);
        decoder->reset();
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(firstImage.constBits()),
                            firstImage.sizeInBytes()), firstPixels);
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
