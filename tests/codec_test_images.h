#pragma once

#include <QByteArray>
#include <QImage>
#include <QPainter>

#include <cmath>

namespace CodecTestImages {
inline QImage colorFrame(QSize size, int frame,
                         QImage::Format format = QImage::Format_RGB32)
{
    QImage image(size, format);
    image.fill(Qt::white);
    QPainter painter(&image);
    const int third = size.width() / 3;
    painter.fillRect(0, 0, third, size.height() / 2, Qt::red);
    painter.fillRect(third, 0, third, size.height() / 2, Qt::green);
    painter.fillRect(2 * third, 0, size.width() - 2 * third,
                     size.height() / 2, Qt::blue);
    painter.fillRect(frame * 8, size.height() - 40, 20, 20, Qt::black);
    return image;
}

inline double meanColorError(const QImage &expected, const QImage &actual)
{
    double error = 0;
    for (int y = 0; y < expected.height(); ++y) {
        for (int x = 0; x < expected.width(); ++x) {
            const QColor a = expected.pixelColor(x, y);
            const QColor b = actual.pixelColor(x, y);
            error += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                     std::abs(a.blue() - b.blue());
        }
    }
    return error / (expected.width() * expected.height() * 3);
}

inline QByteArray pixelBytes(const QImage &image)
{
    return QByteArray(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
}
}
