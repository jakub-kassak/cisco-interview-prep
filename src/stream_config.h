#pragma once

#include <QSize>
#include <QString>
#include <QtGlobal>

enum class StreamRole { Sender, Receiver };

struct StreamConfig
{
    StreamRole role = StreamRole::Receiver;
    QString host = "127.0.0.1";
    quint16 port = 5000;
    QSize frameSize{960, 540};
    int framesPerSecond = 15;
    int bitrate = 1'000'000;
};
