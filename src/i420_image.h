#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <expected>

// Packed I420: width*height Y bytes, then quarter-size U and V planes.
// Converts RGB to BT.601 limited-range YUV, discarding alpha.
std::expected<QByteArray, QString> imageToI420(const QImage &image);

// OpenH264 uses a common U/V stride. The returned image owns its RGB pixels.
std::expected<QImage, QString> imageFromI420(
    const unsigned char *yPlane, const unsigned char *uPlane,
    const unsigned char *vPlane, QSize size, int yStride, int uvStride);
