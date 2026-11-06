#include "paint_stream_window.h"

#include "image_view.h"
#include "paint_canvas.h"
#include "stream_session.h"

#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <utility>

PaintStreamWindow::PaintStreamWindow(StreamConfig config, QWidget *parent)
    : QMainWindow(parent)
    , config_(std::move(config))
    , session_(new StreamSession(config_, this))
    , captureTimer_(new QTimer(this))
    , connectionButton_(new QPushButton(this))
{
    const bool sending = config_.role == StreamRole::Sender;
    setWindowTitle(sending ? "PaintStream — sender" : "PaintStream — receiver");
    resize(1000, 640);
    auto *toolbar = addToolBar("Stream");
    toolbar->setMovable(false);
    connectionButton_->setObjectName("connectionButton");
    toolbar->addWidget(connectionButton_);
    if (sending) {
        setupCanvas(toolbar);
    } else {
        setupDisplay();
    }
    connect(connectionButton_, &QPushButton::clicked,
            this, &PaintStreamWindow::toggleConnection);
    connect(session_, &StreamSession::statusChanged, this, [this](const QString &message) {
        statusBar()->showMessage(message);
    });
    connect(session_, &StreamSession::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage("Error: " + message);
    });
}

void PaintStreamWindow::setupCanvas(QToolBar *toolbar)
{
    canvas_ = new PaintCanvas;
    canvas_->setObjectName("paintCanvas");
    auto *scroll = new QScrollArea;
    scroll->setWidget(canvas_);
    scroll->setAlignment(Qt::AlignCenter);
    setCentralWidget(scroll);
    auto *clearButton = new QPushButton("Clear", this);
    clearButton->setObjectName("clearButton");
    toolbar->addWidget(clearButton);
    connect(clearButton, &QPushButton::clicked, canvas_, &PaintCanvas::clear);
    captureTimer_->setTimerType(Qt::PreciseTimer);
    captureTimer_->setInterval(qRound(1000.0 / config_.framesPerSecond));
    connect(captureTimer_, &QTimer::timeout, this, &PaintStreamWindow::captureFrame);
    connect(session_, &StreamSession::connectionChanged, this, [this](bool connected) {
        if (connected) {
            captureTimer_->start();
        } else {
            captureTimer_->stop();
        }
    });
}

void PaintStreamWindow::setupDisplay()
{
    auto *view = new ImageView(this);
    view->setObjectName("videoView");
    setCentralWidget(view);
    connect(session_, &StreamSession::imageReady, view, &ImageView::setImage);
}

PaintStreamWindow::~PaintStreamWindow()
{
    captureTimer_->stop();
    session_->stop();
}

void PaintStreamWindow::start()
{
    session_->start();
    connectionButton_->setText(config_.role == StreamRole::Sender ? "Disconnect" : "Stop listening");
}

void PaintStreamWindow::toggleConnection()
{
    if (session_->isRunning()) {
        captureTimer_->stop();
        session_->stop();
        connectionButton_->setText(config_.role == StreamRole::Sender ? "Connect" : "Listen");
    } else {
        start();
    }
}

void PaintStreamWindow::captureFrame()
{
    session_->submitFrame(canvas_->snapshot());
}
