#pragma once

#include <QtGlobal>

namespace TransportLimits {
// Wire format: big-endian quint32 payload length, followed by payload bytes.
inline constexpr quint32 maxPayloadBytes = 16 * 1024 * 1024;
// Maximum Qt socket write-buffer size, including message headers.
inline constexpr qint64 maxPendingBytes = 32 * 1024 * 1024;
}
