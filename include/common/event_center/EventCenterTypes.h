#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <optional>

enum class SecurityEventType {
    MqttConnectionLost,
    DevicePresenceLost,
    VideoStreamLost,
    MediaServerUnhealthy,
    LocalControlPublishFailed,
    LocalSafetyStopPublishFailed,
    LocalSafetyStopUnavailable,
    ManualIncident,
};

enum class SecurityEventSeverity { Low, Medium, High, Critical };
enum class SecurityEventState { Open, Acknowledged, Resolved, Closed };
enum class ResolutionSource { None, PlatformObservation, Operator };
enum class CloseDisposition { None, ObservedRecovery, ClosedWithoutObservedRecovery };
enum class EventTransitionKind {
    Created,
    Acknowledged,
    Recurred,
    Recovered,
    ResolvedByOperator,
    Closed,
    ClosedWithoutObservedRecovery,
};

enum class EventOperationError {
    None,
    NotInitialized,
    Stopped,
    StorageUnavailable,
    InvalidInput,
    NotFound,
    IllegalTransition,
};

struct EventControlAttemptSummary
{
    QString attemptId;
    QDateTime observedAtUtc;
    QString action;
    QString localOutcome;
    QString source;
    QString executionConfirmation = QStringLiteral("unavailable");
    QString targetDeviceId;
    QString identitySource = QStringLiteral("url-derived");
};

struct SecurityEventHistoryEntry
{
    quint64 revision = 0;
    EventTransitionKind transition = EventTransitionKind::Created;
    std::optional<SecurityEventState> fromState;
    SecurityEventState toState = SecurityEventState::Open;
    QDateTime atUtc;
    QString source;
    QString actor;
    QString actorAssurance = QStringLiteral("unverified-local");
    QString note;
};

struct SecurityEventRecord
{
    QString eventId;
    SecurityEventType eventType = SecurityEventType::ManualIncident;
    SecurityEventSeverity severity = SecurityEventSeverity::Medium;
    SecurityEventState state = SecurityEventState::Open;
    QString localResourceId;
    QString deviceId;
    QString displayNameSnapshot;
    QDateTime openedAtUtc;
    QDateTime lastObservedAtUtc;
    QDateTime acknowledgedAtUtc;
    QDateTime resolvedAtUtc;
    QDateTime closedAtUtc;
    ResolutionSource resolutionSource = ResolutionSource::None;
    CloseDisposition closeDisposition = CloseDisposition::None;
    quint64 eventRevision = 1;
    quint64 occurrenceCount = 1;
    QString actor;
    QString actorAssurance = QStringLiteral("unverified-local");
    QString identitySource = QStringLiteral("url-derived");
    QString note;
    QList<EventControlAttemptSummary> linkedControlAttempts;
    QStringList evidenceIds;
    QList<SecurityEventHistoryEntry> history;
};

struct SecurityEventTombstone
{
    QString eventId;
    SecurityEventType eventType = SecurityEventType::ManualIncident;
    QString localResourceId;
    QString deviceId;
    QDateTime openedAtUtc;
    QDateTime closedAtUtc;
    CloseDisposition closeDisposition = CloseDisposition::None;
    QStringList evidenceIds;
    quint64 eventRevision = 1;
};

struct EventObservation
{
    SecurityEventType eventType = SecurityEventType::ManualIncident;
    SecurityEventSeverity severity = SecurityEventSeverity::Medium;
    QString localResourceId;
    QString deviceId;
    QString displayNameSnapshot;
    QString identitySource = QStringLiteral("url-derived");
    QString source;
    QString note;
    std::optional<EventControlAttemptSummary> controlAttempt;
};

struct EventOperationResult
{
    bool changed = false;
    QString eventId;
    EventOperationError error = EventOperationError::None;
    QString message;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return error == EventOperationError::None;
    }
};

struct EventCenterSummary
{
    int activeCount = 0;
    SecurityEventSeverity highestSeverity = SecurityEventSeverity::Low;
};

struct EventResourceDescriptor
{
    QString localResourceId;
    QString deviceId;
    QString displayName;
    QString identitySource;
};

[[nodiscard]] bool isSystemEvent(SecurityEventType type) noexcept;
[[nodiscard]] QString securityEventTypeName(SecurityEventType type);
[[nodiscard]] QString securityEventSeverityName(SecurityEventSeverity severity);
[[nodiscard]] QString securityEventStateName(SecurityEventState state);
[[nodiscard]] QString resolutionSourceName(ResolutionSource source);
[[nodiscard]] QString closeDispositionName(CloseDisposition disposition);
[[nodiscard]] QString eventTransitionKindName(EventTransitionKind transition);

[[nodiscard]] std::optional<SecurityEventType> securityEventTypeFromName(const QString &name);
[[nodiscard]] std::optional<SecurityEventSeverity> securityEventSeverityFromName(const QString &name);
[[nodiscard]] std::optional<SecurityEventState> securityEventStateFromName(const QString &name);
[[nodiscard]] std::optional<ResolutionSource> resolutionSourceFromName(const QString &name);
[[nodiscard]] std::optional<CloseDisposition> closeDispositionFromName(const QString &name);
[[nodiscard]] std::optional<EventTransitionKind> eventTransitionKindFromName(const QString &name);

Q_DECLARE_METATYPE(SecurityEventRecord)
Q_DECLARE_METATYPE(EventCenterSummary)
Q_DECLARE_METATYPE(EventOperationResult)
Q_DECLARE_METATYPE(EventResourceDescriptor)
