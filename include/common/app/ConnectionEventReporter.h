#pragma once

#include <QString>
#include <QJsonObject>

#include "app/ConnectionBindingRegistry.h"
#include "logging/LogTypes.h"

class LogManager;
class UserMessageService;

/** @brief Centralizes connection system logs, user events and audit records. */
class ConnectionEventReporter final
{
public:
    ConnectionEventReporter(
        LogManager *logManager,
        UserMessageService *userMessageService
    );

    [[nodiscard]] LogContext context(const ConnectionBinding &binding) const;
    void logSystem(
        LogLevel level,
        const QString &category,
        const QString &event,
        const QString &message,
        const QJsonObject &fields = {},
        const LogContext &context = {},
        bool aggregate = false
    );
    void logDeviceState(const ConnectionBinding &binding, DeviceStatus status);
    void publish(
        UserEventType type,
        UserFailureReason reason,
        const ConnectionBinding *binding,
        const QString &displayName = {}
    );
    void audit(
        AuditAction action,
        AuditResult result,
        const ConnectionBinding *binding,
        const QString &displayName,
        const QString &reason = {}
    );
    [[nodiscard]] static UserFailureReason userReason(PlaybackErrorCode code);

private:
    LogManager *logManager_ = nullptr;
    UserMessageService *userMessageService_ = nullptr;
};
