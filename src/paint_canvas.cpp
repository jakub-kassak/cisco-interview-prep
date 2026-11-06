#include "paint_canvas.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>

namespace {
constexpr int canvasWidth = 960;
constexpr int canvasHeight = 540;
constexpr int penWidth = 4;
}

PaintCanvas::PaintCanvas(QWidget *parent)
    : QWidget(parent)
    , image_(canvasWidth, canvasHeight, QImage::Format_RGB32)
{
    // Keep bitmap coordinates identical to widget coordinates, even when the
    // surrounding window is resized. The scroll area handles small windows.
    setFixedSize(image_.size());
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
    image_.fill(Qt::white);
}

void PaintCanvas::clear()
{
    drawing_ = false;
    image_.fill(Qt::white);
    update();
}

QImage PaintCanvas::snapshot() const
{
    return image_;
}

void PaintCanvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    const QRect dirty = event->rect();
    painter.drawImage(dirty, image_, dirty);
}

void PaintCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        drawing_ = true;
        lastPosition_ = event->position().toPoint();
        // A single click should leave a dot too.
        drawLineTo(lastPosition_);
    }
}

void PaintCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (drawing_ && event->buttons().testFlag(Qt::LeftButton)) {
        drawLineTo(event->position().toPoint());
    }
}

void PaintCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (drawing_ && event->button() == Qt::LeftButton) {
        drawLineTo(event->position().toPoint());
        drawing_ = false;
    }
}

void PaintCanvas::drawLineTo(const QPoint &position)
{
    {
        // Paint into the bitmap here; only paintEvent paints on the widget.
        QPainter painter(&image_);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::black, penWidth, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(lastPosition_, position);
    }
    lastPosition_ = position;
    update(); // Schedule a repaint; Qt can combine frequent mouse updates.
}
