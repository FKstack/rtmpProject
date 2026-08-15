#pragma once

#include <QByteArray>
#include <QtGlobal>

#include "device_control/DeviceControlTypes.h"

class DeviceCommandCodec final
{
public:
    [[nodiscard]] static QByteArray encode(DeviceCommand command,
                                           qint64 timestampMs);
};
