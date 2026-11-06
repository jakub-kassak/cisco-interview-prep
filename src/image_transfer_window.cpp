#include "image_transfer_window.h"

#include "image_view.h"
#include "tcp_receiver.h"
#include "tcp_sender.h"
#include "transport_limits.h"

#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>

#include <utility>

ImageTransferWindow::ImageTransferWindow(TransferOptions options, QWidget *parent)
    : QMainWindow(parent)
    , options_(std::move(options))
    , view_(new ImageView(this))
{
    setCentralWidget(view_);
    resize(1000, 640);
    if (options_.mode == TransferMode::Receive) {
        setWindowTitle("Image transfer — receive");
        setupReceiver();
    } else {
        setWindowTitle("Image transfer — send");
        setupSender();
    }
}

bool ImageTransferWindow::start()
{
    if (receiver_) {
        const QHostAddress address(options_.host);
        if (address.isNull() || !receiver_->listen(address, options_.port)) {
            qCritical() << "Could not listen on" << options_.host << options_.port;
            return false;
        }
        showStatus(QString("Listening on %1:%2").arg(address.toString()).arg(options_.port));
    } else {
        showStatus("Connecting…");
        sender_->connectToHost(options_.host, options_.port);
    }
    return true;
}

void ImageTransferWindow::setupReceiver()
{
    receiver_ = new TcpReceiver(this);
    connect(receiver_, &TcpReceiver::connected, this, [this] {
        showStatus("Sender connected; waiting for an image.");
    });
    connect(receiver_, &TcpReceiver::disconnected, this, [this] {
        showStatus("Sender disconnected; listening for another sender.");
    });
    connect(receiver_, &TcpReceiver::errorOccurred,
            this, &ImageTransferWindow::showStatus);
    connect(receiver_, &TcpReceiver::messageReceived,
            this, &ImageTransferWindow::displayReceivedImage);
}

void ImageTransferWindow::setupSender()
{
    sender_ = new TcpSender(this);
    auto *toolbar = addToolBar("Send");
    toolbar->setMovable(false);
    sendButton_ = new QPushButton("Choose PNG and send…", this);
    toolbar->addWidget(sendButton_);
    sendButton_->setEnabled(false);

    connect(sendButton_, &QPushButton::clicked,
            this, &ImageTransferWindow::chooseAndSendFile);
    connect(sender_, &TcpSender::connected, this, [this] {
        sendButton_->setEnabled(true);
        showStatus("Connected; choose a PNG to send.");
        if (!options_.filePath.isEmpty()) {
            sendFile(options_.filePath);
        }
    });
    connect(sender_, &TcpSender::disconnected, this, [this] {
        sendButton_->setEnabled(false);
        showStatus("Disconnected. Restart sender to reconnect.");
    });
    connect(sender_, &TcpSender::errorOccurred,
            this, &ImageTransferWindow::showStatus);
    connect(sender_, &TcpSender::drained, this, [this] {
        showStatus("Outgoing buffer drained; delivery is not acknowledged.");
    });
}

void ImageTransferWindow::chooseAndSendFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Send PNG", {}, "PNG images (*.png)");
    if (!path.isEmpty()) {
        sendFile(path);
    }
}

void ImageTransferWindow::sendFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 1 ||
        file.size() > TransportLimits::maxPayloadBytes) {
        showStatus("Cannot read file; choose a PNG no larger than 16 MiB.");
        return;
    }
    // Limit the read even if another process grows the file meanwhile.
    QByteArray bytes = file.read(TransportLimits::maxPayloadBytes + 1LL);
    if (file.error() != QFileDevice::NoError ||
        bytes.size() > TransportLimits::maxPayloadBytes) {
        showStatus("Could not read the complete file within the size limit.");
        return;
    }
    QImage image = QImage::fromData(bytes, "PNG");
    if (image.isNull()) {
        showStatus("The selected file is not a readable PNG.");
        return;
    }
    if (!sender_->sendMessage(std::move(bytes))) {
        showStatus("Image not queued: disconnected or outgoing queue is full.");
        return;
    }
    view_->setImage(std::move(image));
    showStatus("Image queued for sending.");
}

void ImageTransferWindow::displayReceivedImage(QByteArray bytes)
{
    // PNG decoding belongs to the application layer, not the TCP receiver.
    QImage image = QImage::fromData(bytes, "PNG");
    if (image.isNull()) {
        showStatus("Received a message, but it is not a readable PNG.");
        return;
    }
    const QString description = QString("Received %1 × %2 PNG (%3 bytes).")
        .arg(image.width()).arg(image.height()).arg(bytes.size());
    view_->setImage(std::move(image));
    showStatus(description);
}

void ImageTransferWindow::showStatus(const QString &message)
{
    statusBar()->showMessage(message);
}
