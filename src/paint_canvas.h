#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

class PaintCanvas : public QWidget
{
public:
    explicit PaintCanvas(QWidget *parent = nullptr);

    void clear();
    // QImage uses implicit sharing: callers get a snapshot that stays valid
    // when the next stroke changes the canvas. Call this on the GUI thread.
    QImage snapshot() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void extendStroke(const QPoint &position);

    QImage image_;
    QPoint lastPosition_;
    bool drawing_ = false;
};
