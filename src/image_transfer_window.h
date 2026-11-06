#pragma once

#include "transfer_options.h"

#include <QByteArray>
#include <QMainWindow>

class ImageView;
class QPushButton;
class TcpReceiver;
class TcpSender;

class ImageTransferWindow : public QMainWindow
{
public:
    explicit ImageTransferWindow(TransferOptions options, QWidget *parent = nullptr);
    bool start();

private:
    void setupReceiver();
    void setupSender();
    void chooseAndSendFile();
    void sendFile(const QString &path);
    void displayReceivedImage(QByteArray bytes);
    void showStatus(const QString &message);

    const TransferOptions options_;
    ImageView *view_;
    TcpReceiver *receiver_ = nullptr;
    TcpSender *sender_ = nullptr;
    QPushButton *sendButton_ = nullptr;
};
