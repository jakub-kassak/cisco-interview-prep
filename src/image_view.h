#pragma once

#include <QImage>
#include <QWidget>

class ImageView : public QWidget
{
public:
    explicit ImageView(QWidget *parent = nullptr);
    // GUI thread only. Keeps its own implicitly shared image value.
    void setImage(QImage image);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage image_;
};
