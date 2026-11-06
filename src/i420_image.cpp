#include "i420_image.h"

#include <QImage>

#include <algorithm>
#include <limits>

std::expected<QByteArray, QString> imageToI420(const QImage &image)
{
    const int width = image.width();
    const int height = image.height();
    if (image.isNull() || width % 2 != 0 || height % 2 != 0) {
        return std::unexpected(QStringLiteral("I420 requires a nonempty image with even dimensions."));
    }
    const qint64 ySize = qint64(width) * height;
    if (ySize > qint64(std::numeric_limits<int>::max()) * 2 / 3) {
        return std::unexpected(QStringLiteral("Image is too large for the codec."));
    }

    // RGB888 has an explicit R,G,B byte order on every platform. Read scanlines
    // separately because Qt can pad the end of each row.
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    if (rgb.isNull()) {
        return std::unexpected(QStringLiteral("Could not convert the image to RGB."));
    }
    QByteArray pixels(qsizetype(ySize * 3 / 2), '\0');
    auto *yPlane = reinterpret_cast<unsigned char *>(pixels.data());
    auto *uPlane = yPlane + ySize;
    auto *vPlane = uPlane + ySize / 4;

    for (int y = 0; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            int redSum = 0;
            int greenSum = 0;
            int blueSum = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const auto *row = rgb.constScanLine(y + dy);
                for (int dx = 0; dx < 2; ++dx) {
                    const auto *pixel = row + 3 * (x + dx);
                    const int r = pixel[0];
                    const int g = pixel[1];
                    const int b = pixel[2];
                    yPlane[qsizetype(y + dy) * width + x + dx] =
                        16 + ((66 * r + 129 * g + 25 * b + 128) >> 8);
                    redSum += r;
                    greenSum += g;
                    blueSum += b;
                }
            }
            // One chroma sample represents the average of a 2x2 pixel block.
            const int r = (redSum + 2) / 4;
            const int g = (greenSum + 2) / 4;
            const int b = (blueSum + 2) / 4;
            const qsizetype chroma = qsizetype(y / 2) * (width / 2) + x / 2;
            uPlane[chroma] = 128 + ((-38 * r - 74 * g + 112 * b + 128) >> 8);
            vPlane[chroma] = 128 + ((112 * r - 94 * g - 18 * b + 128) >> 8);
        }
    }
    return pixels;
}

std::expected<QImage, QString> imageFromI420(
    const unsigned char *yPlane, const unsigned char *uPlane,
    const unsigned char *vPlane, QSize size, int yStride, int uvStride)
{
    if (!yPlane || !uPlane || !vPlane || size.width() < 1 || size.height() < 1 ||
        size.width() % 2 != 0 || size.height() % 2 != 0 ||
        yStride < size.width() || uvStride < size.width() / 2) {
        return std::unexpected(QStringLiteral("Invalid I420 planes, dimensions, or strides."));
    }
    QImage image(size, QImage::Format_RGB888);
    if (image.isNull()) {
        return std::unexpected(QStringLiteral("Could not allocate the decoded image."));
    }
    for (int y = 0; y < size.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            const int c = int(yPlane[qsizetype(y) * yStride + x]) - 16;
            const qsizetype chroma = qsizetype(y / 2) * uvStride + x / 2;
            const int d = int(uPlane[chroma]) - 128;
            const int e = int(vPlane[chroma]) - 128;
            row[3 * x] = std::clamp((298 * c + 409 * e + 128) >> 8, 0, 255);
            row[3 * x + 1] = std::clamp((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255);
            row[3 * x + 2] = std::clamp((298 * c + 516 * d + 128) >> 8, 0, 255);
        }
    }
    return image;
}
