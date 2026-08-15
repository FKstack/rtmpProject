#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

#include "device_control/DeviceControlTypes.h"

/** Pure JSON parser for the device/status heartbeat contract. */
class DeviceHeartbeatCodec final
{
public:
    static constexpr qsizetype kMaximumClientIdLength = 128;

    [[nodiscard]] static std::optional<DeviceHeartbeat> decode(
        const QByteArray &payload,
        qint64 receivedAtMonotonicMs,
        QString *error = nullptr);
};
