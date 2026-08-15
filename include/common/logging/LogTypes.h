#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

#include <cstdint>

enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

struct LogContext
{
    std::uint64_t deviceId = 0;
    QString deviceName;
    QString url;
};

struct SystemLogEntry
{
    QDateTime timestampUtc;
    LogLevel level = LogLevel::Info;
    QString module;
    QString eventName;
    LogContext context;
    QString sanitizedUrl;
    QString message;
    QJsonObject fields;
    QString threadId;
    int repeatedCount = 1;
};

enum class AuditAction {
    Login,
    Logout,
    AddDevice,
    RemoveDevice,
    UpdateDeviceName,
    UpdateDeviceConnection,
    UpdateUserConfiguration,
    UpdatePermission,
    ImportConfiguration,
    ExportConfiguration,
    ClearData,
    RestoreDefaults,
    SoftwareUpgrade,
    DeviceUpgrade,
    ManualReconnect,
    ControlCommandAttempt,
    ControlSessionTransition,
};

enum class AuditResult {
    Success,
    Failure,
    Cancelled,
    Submitted,
    Rejected,
    PublishFailed,
};

struct AuditRecord
{
    QDateTime timestampUtc;
    QString actor;
    AuditAction action = AuditAction::Login;
    QString targetType;
    QString targetId;
    AuditResult result = AuditResult::Success;
    QString reason;
    QJsonObject beforeValues;
    QJsonObject afterValues;
    QString source;
};

struct LogFileOptions
{
    qint64 maximumFileBytes = 10 * 1024 * 1024;
    int retainedFileCount = 5;
    int retentionDays = 14;
    qint64 maximumTotalBytes = 64 * 1024 * 1024;
    int queueCapacity = 4'096;
};

struct LoggingOptions
{
    QString directoryPath;
    LogLevel minimumLevel = LogLevel::Info;
    QHash<QString, LogLevel> moduleMinimumLevels;
    LogFileOptions systemFile;
    LogFileOptions auditFile {
        10 * 1024 * 1024,
        20,
        180,
        256 * 1024 * 1024,
        1'024
    };
    int repeatWindowMs = 60'000;
    int shutdownFlushTimeoutMs = 2'000;
    int recoveryRetryMs = 60'000;
    int userMessageRepeatWindowMs = 60'000;
    bool systemFileEnabled = true;
    bool auditFileEnabled = true;
    bool consoleEnabled = false;
};

Q_DECLARE_METATYPE(LogLevel)
Q_DECLARE_METATYPE(SystemLogEntry)
Q_DECLARE_METATYPE(AuditRecord)
