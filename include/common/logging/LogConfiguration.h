#pragma once

#include <QString>

#include "logging/LogTypes.h"

class LogConfiguration final
{
public:
    LogConfiguration() = delete;

    [[nodiscard]] static LoggingOptions defaults();
    [[nodiscard]] static QString defaultFilePath();
    [[nodiscard]] static LoggingOptions load(
        const QString &filePath,
        QString *errorMessage = nullptr
    );
};
