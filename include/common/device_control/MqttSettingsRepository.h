#pragma once

#include <QString>

#include "device_control/DeviceControlTypes.h"

struct MqttSettingsLoadResult
{
    MqttConnectionOptions options;
    QString error;
    bool fileExists = false;
    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

class MqttSettingsRepository final
{
public:
    static constexpr int kSchemaVersion = 1;

    explicit MqttSettingsRepository(QString filePath = {});
    [[nodiscard]] MqttSettingsLoadResult load() const;
    [[nodiscard]] bool save(const MqttConnectionOptions &options,
                            QString *error = nullptr) const;
    [[nodiscard]] static bool validate(const MqttConnectionOptions &options,
                                       QString *error = nullptr);
    [[nodiscard]] QString filePath() const;

private:
    QString filePath_;
};
