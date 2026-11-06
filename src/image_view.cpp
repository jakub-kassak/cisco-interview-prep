#include "image_view.h"

#include <QPainter>

#include <utility>

ImageView::ImageView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ImageView::setImage(QImage image)
{
    image_ = std::move(image);
    update();
}

void ImageView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(32, 34, 38));

    if (image_.isNull()) {
        painter.setPen(QColor(220, 222, 226));
        painter.drawText(rect(), Qt::AlignCenter, "No image loaded");
        return;
    }

    // Scale the destination rectangle, leaving the original image intact.
    const QSize displaySize = image_.size().scaled(size(), Qt::KeepAspectRatio);
    QRect target(QPoint(0, 0), displaySize);
    target.moveCenter(rect().center());

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, image_);
}
