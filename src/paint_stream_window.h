#pragma once

#include "stream_config.h"

#include <QMainWindow>

class PaintCanvas;
class QPushButton;
class QTimer;
class StreamSession;

class PaintStreamWindow : public QMainWindow
{
public:
    explicit PaintStreamWindow(StreamConfig config, QWidget *parent = nullptr);
    ~PaintStreamWindow() override;
    void start();

private:
    void toggleConnection();
    void captureFrame();

    const StreamConfig config_;
    StreamSession *session_;
    PaintCanvas *canvas_ = nullptr;
    QTimer *captureTimer_;
    QPushButton *connectionButton_;
};
