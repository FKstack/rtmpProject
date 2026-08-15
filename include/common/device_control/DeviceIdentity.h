#pragma once

#include <QString>

#include <optional>

class DeviceIdentity final
{
public:
    [[nodiscard]] static bool isValid(const QString &deviceId);
    [[nodiscard]] static std::optional<QString> fromRtmpUrl(
        const QString &streamUrl);
};
