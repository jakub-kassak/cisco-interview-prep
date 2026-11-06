#pragma once

#include "stream_config.h"

#include <QImage>
#include <QObject>
#include <QThread>

class StreamWorker;

// GUI-side controller. All public methods are called on the GUI thread.
// The owner keeps it alive through callbacks and destroys it before app exit.
class StreamSession : public QObject
{
    Q_OBJECT
public:
    explicit StreamSession(StreamConfig config, QObject *parent = nullptr);
    ~StreamSession() override;
    void start();
    void stop();
    // Returns false if the session cannot accept another capture.
    bool submitFrame(QImage image);
    bool canCapture() const;
    bool isRunning() const;
    bool isConnected() const;

signals:
    void statusChanged(QString message);
    void errorOccurred(QString message);
    void connectionChanged(bool connected);
    void listening(quint16 port);
    void frameProcessed(bool sent);
    void imageReady(QImage image);

private:
    Q_SIGNAL void frameSubmitted(QImage image);
    Q_SIGNAL void imageAcknowledged(quint64 ticket);
    void connectWorker(quint64 generation);

    const StreamConfig config_;
    QThread workerThread_;
    StreamWorker *worker_ = nullptr;
    quint64 generation_ = 0;
    bool running_ = false;
    bool connected_ = false;
    bool captureBusy_ = false;
};
