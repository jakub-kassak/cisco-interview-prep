#pragma once

#include <QString>
#include <QtGlobal>

enum class TransferMode { Send, Receive };

struct TransferOptions
{
    TransferMode mode = TransferMode::Receive;
    QString host = "127.0.0.1";
    quint16 port = 5000;
    QString filePath;
};
