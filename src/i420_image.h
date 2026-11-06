#pragma once

#include <QByteArray>
#include <QString>

#include <expected>

class QImage;

// Packed I420: width*height Y bytes, then quarter-size U and V planes.
// Converts RGB to BT.601 limited-range YUV, discarding alpha.
std::expected<QByteArray, QString> imageToI420(const QImage &image);
