#include "stream_session.h"

#include "stream_worker.h"

#include <utility>

StreamSession::StreamSession(StreamConfig config, QObject *parent)
    : QObject(parent)
    , config_(std::move(config))
{
    workerThread_.setObjectName(QStringLiteral("video-worker"));
}

StreamSession::~StreamSession()
{
    stop();
}

void StreamSession::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (running_) {
        return;
    }
    const quint64 generation = ++generation_;
    running_ = true;
    connected_ = false;
    captureBusy_ = false;
    worker_ = new StreamWorker(config_);
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::started, worker_, &StreamWorker::start);
    // Qt processes this deferred deletion when the worker thread finishes.
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(worker_, &StreamWorker::statusChanged, this, [this, generation](QString message) {
        if (generation == generation_) {
            emit statusChanged(std::move(message));
        }
    });
    connect(worker_, &StreamWorker::errorOccurred, this, [this, generation](QString message) {
        if (generation == generation_) {
            emit errorOccurred(std::move(message));
        }
    });
    connect(worker_, &StreamWorker::listening, this, [this, generation](quint16 port) {
        if (generation == generation_) {
            emit listening(port);
        }
    });
    connect(worker_, &StreamWorker::connectionChanged, this, [this, generation](bool connected) {
        if (generation == generation_) {
            connected_ = connected;
            emit connectionChanged(connected);
        }
    });
    connect(worker_, &StreamWorker::frameProcessed, this, [this, generation](bool sent) {
        if (generation == generation_) {
            captureBusy_ = false;
            emit frameProcessed(sent);
        }
    });
    connect(worker_, &StreamWorker::imageReady, this,
            [this, generation](QImage image, quint64 ticket) {
        if (generation != generation_) {
            return;
        }
        emit imageReady(std::move(image));
        // An image callback may stop or restart the session.
        if (generation == generation_ && running_) {
            QMetaObject::invokeMethod(worker_, [worker = worker_, ticket] {
                worker->acknowledgeImage(ticket);
            }, Qt::QueuedConnection);
        }
    });
    workerThread_.start();
}

void StreamSession::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!running_) {
        return;
    }
    ++generation_; // Ignore already queued notifications from the old worker.
    running_ = false;
    connected_ = false;
    captureBusy_ = false;
    // Shutdown runs on the socket/codec owning thread. This bounded wait is
    // only for teardown; normal networking never blocks the GUI thread.
    QMetaObject::invokeMethod(worker_, &StreamWorker::stop, Qt::BlockingQueuedConnection);
    workerThread_.quit();
    workerThread_.wait();
    worker_ = nullptr;
    emit connectionChanged(false);
    emit statusChanged(QStringLiteral("Stopped."));
}

bool StreamSession::submitFrame(QImage image)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!canCapture()) {
        return false;
    }
    captureBusy_ = true;
    QMetaObject::invokeMethod(worker_, [worker = worker_, image = std::move(image)]() mutable {
        worker->processFrame(std::move(image));
    }, Qt::QueuedConnection);
    return true;
}

bool StreamSession::canCapture() const
{
    return config_.role == StreamRole::Sender && running_ && connected_ && !captureBusy_;
}

bool StreamSession::isRunning() const
{
    return running_;
}

bool StreamSession::isConnected() const
{
    return connected_;
}
