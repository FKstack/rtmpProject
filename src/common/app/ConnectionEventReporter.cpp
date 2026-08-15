#include "app/ConnectionEventReporter.h"

#include "logging/LogManager.h"
#include "logging/UserMessageService.h"

namespace {

QString currentAuditActor()
{
    QString actor = qEnvironmentVariable("USERNAME").trimmed();
    if (actor.isEmpty()) actor = qEnvironmentVariable("USER").trimmed();
    return actor.isEmpty() ? QStringLiteral("local-user") : actor;
}

} // namespace

ConnectionEventReporter::ConnectionEventReporter(
    LogManager *logManager,
    UserMessageService *userMessageService
)
    : logManager_(logManager)
    , userMessageService_(userMessageService)
{
}

LogContext ConnectionEventReporter::context(const ConnectionBinding &binding) const
{
    return {binding.streamId, binding.displayName, binding.url};
}

void ConnectionEventReporter::logSystem(
    LogLevel level,
    const QString &category,
    const QString &event,
    const QString &message,
    const QJsonObject &fields,
    const LogContext &context,
    bool aggregate
)
{
    if (logManager_ != nullptr) {
        logManager_->logSystem(
            level, category, event, message, fields, context, aggregate
        );
    }
}

void ConnectionEventReporter::logDeviceState(
    const ConnectionBinding &binding,
    DeviceStatus status
)
{
    if (logManager_ == nullptr) return;
    QString state;
    LogLevel level = LogLevel::Info;
    bool aggregate = false;
    switch (status) {
    case DeviceStatus::Disconnected: state = QStringLiteral("disconnected"); break;
    case DeviceStatus::Connecting: state = QStringLiteral("connecting"); aggregate = true; break;
    case DeviceStatus::Playing: state = QStringLiteral("playing"); break;
    case DeviceStatus::Reconnecting: state = QStringLiteral("reconnecting"); aggregate = true; break;
    case DeviceStatus::Error: state = QStringLiteral("error"); level = LogLevel::Warning; aggregate = true; break;
    }
    logManager_->logSystem(
        level, QStringLiteral("device"), QStringLiteral("status_changed"),
        QStringLiteral("Device status changed."),
        {{QStringLiteral("state"), state}}, context(binding), aggregate
    );
}

void ConnectionEventReporter::publish(
    UserEventType type,
    UserFailureReason reason,
    const ConnectionBinding *binding,
    const QString &displayName
)
{
    if (userMessageService_ == nullptr) return;
    userMessageService_->publish({
        type, reason,
        binding != nullptr ? binding->streamId : 0,
        binding != nullptr ? binding->displayName : displayName
    });
}

void ConnectionEventReporter::audit(
    AuditAction action,
    AuditResult result,
    const ConnectionBinding *binding,
    const QString &displayName,
    const QString &reason
)
{
    if (logManager_ == nullptr) return;
    AuditRecord record;
    record.actor = currentAuditActor();
    record.action = action;
    record.targetType = QStringLiteral("Camera");
    record.targetId = binding != nullptr
                          ? QString::number(binding->streamId)
                          : displayName;
    record.result = result;
    record.reason = reason;
    record.source = QStringLiteral("local-ui");
    QJsonObject values;
    values.insert(QStringLiteral("deviceName"),
                  binding != nullptr ? binding->displayName : displayName);
    if (binding != nullptr) {
        values.insert(QStringLiteral("connectionUrl"), binding->url);
    }
    if (action == AuditAction::RemoveDevice) record.beforeValues = values;
    else record.afterValues = values;
    logManager_->logAudit(record);
}

UserFailureReason ConnectionEventReporter::userReason(PlaybackErrorCode code)
{
    switch (code) {
    case PlaybackErrorCode::ConnectionTimeout: return UserFailureReason::ConnectionTimeout;
    case PlaybackErrorCode::HostUnavailable: return UserFailureReason::HostUnavailable;
    case PlaybackErrorCode::AuthenticationFailed: return UserFailureReason::AuthenticationFailed;
    case PlaybackErrorCode::InvalidConfiguration: return UserFailureReason::InvalidConfiguration;
    case PlaybackErrorCode::MediaUnavailable:
    case PlaybackErrorCode::UnsupportedMedia:
    case PlaybackErrorCode::DecodeFailure: return UserFailureReason::MediaUnavailable;
    case PlaybackErrorCode::AlreadyRunning:
    case PlaybackErrorCode::RuntimeInitializationFailed:
    case PlaybackErrorCode::ResourceFailure:
    case PlaybackErrorCode::RetryLimitReached:
    case PlaybackErrorCode::Unknown: return UserFailureReason::Unknown;
    }
    return UserFailureReason::Unknown;
}
