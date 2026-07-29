#include "logging/LogConfiguration.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include "logging/LogManager.h"

namespace {

void readFileOptions(
    QSettings &settings,
    const QString &prefix,
    LogFileOptions *options
)
{
    options->maximumFileBytes = settings.value(
        prefix + QStringLiteral("/maximumFileBytes"),
        options->maximumFileBytes
    ).toLongLong();
    options->retainedFileCount = settings.value(
        prefix + QStringLiteral("/retainedFileCount"),
        options->retainedFileCount
    ).toInt();
    options->retentionDays = settings.value(
        prefix + QStringLiteral("/retentionDays"),
        options->retentionDays
    ).toInt();
    options->maximumTotalBytes = settings.value(
        prefix + QStringLiteral("/maximumTotalBytes"),
        options->maximumTotalBytes
    ).toLongLong();
    options->queueCapacity = settings.value(
        prefix + QStringLiteral("/queueCapacity"),
        options->queueCapacity
    ).toInt();
}

} // namespace

LoggingOptions LogConfiguration::defaults()
{
    LoggingOptions options;
#ifndef NDEBUG
    options.minimumLevel = LogLevel::Debug;
#else
    options.minimumLevel = LogLevel::Info;
#endif
    options.directoryPath = QDir(
        QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation
        )
    ).filePath(QStringLiteral("logs"));
    return options;
}

QString LogConfiguration::defaultFilePath()
{
    return QDir(
        QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation
        )
    ).filePath(QStringLiteral("rtmp-monitor.ini"));
}

LoggingOptions LogConfiguration::load(
    const QString &filePath,
    QString *errorMessage
)
{
    LoggingOptions options = defaults();
    const QString path = filePath.trimmed().isEmpty()
        ? defaultFilePath()
        : filePath.trimmed();
    if (!QFileInfo::exists(path)) {
        return options;
    }

    QSettings settings(path, QSettings::IniFormat);
    options.directoryPath = settings.value(
        QStringLiteral("logging/directoryPath"),
        options.directoryPath
    ).toString();
    LogLevel configuredLevel = options.minimumLevel;
    const QString levelText = settings.value(
        QStringLiteral("system/level")
    ).toString();
    if (!levelText.isEmpty() &&
        !LogManager::parseLevel(levelText, &configuredLevel)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "无效的 system/level：%1"
            ).arg(levelText);
        }
    } else {
        options.minimumLevel = configuredLevel;
    }
    options.consoleEnabled = settings.value(
        QStringLiteral("system/consoleEnabled"),
        options.consoleEnabled
    ).toBool();
    options.repeatWindowMs = settings.value(
        QStringLiteral("system/repeatWindowMs"),
        options.repeatWindowMs
    ).toInt();
    options.shutdownFlushTimeoutMs = settings.value(
        QStringLiteral("logging/shutdownFlushTimeoutMs"),
        options.shutdownFlushTimeoutMs
    ).toInt();
    options.recoveryRetryMs = settings.value(
        QStringLiteral("logging/recoveryRetryMs"),
        options.recoveryRetryMs
    ).toInt();
    options.userMessageRepeatWindowMs = settings.value(
        QStringLiteral("userMessages/repeatWindowMs"),
        options.userMessageRepeatWindowMs
    ).toInt();
    readFileOptions(settings, QStringLiteral("system"), &options.systemFile);
    readFileOptions(settings, QStringLiteral("audit"), &options.auditFile);

    settings.beginGroup(QStringLiteral("system.modules"));
    for (const QString &module : settings.childKeys()) {
        LogLevel level = LogLevel::Info;
        if (LogManager::parseLevel(settings.value(module).toString(), &level)) {
            options.moduleMinimumLevels.insert(module.toLower(), level);
        }
    }
    settings.endGroup();
    return options;
}
