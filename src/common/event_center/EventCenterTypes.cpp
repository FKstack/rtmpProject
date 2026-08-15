#include "event_center/EventCenterTypes.h"

namespace {

template <typename Enum>
std::optional<Enum> parseNamedEnum(
    const QString &name,
    std::initializer_list<std::pair<Enum, const char *>> values)
{
    for (const auto &[value, text] : values) {
        if (name == QLatin1String(text)) return value;
    }
    return std::nullopt;
}

} // namespace

bool isSystemEvent(SecurityEventType type) noexcept
{
    return type != SecurityEventType::ManualIncident;
}

QString securityEventTypeName(SecurityEventType type)
{
    switch (type) {
    case SecurityEventType::MqttConnectionLost: return QStringLiteral("MqttConnectionLost");
    case SecurityEventType::DevicePresenceLost: return QStringLiteral("DevicePresenceLost");
    case SecurityEventType::VideoStreamLost: return QStringLiteral("VideoStreamLost");
    case SecurityEventType::MediaServerUnhealthy: return QStringLiteral("MediaServerUnhealthy");
    case SecurityEventType::LocalControlPublishFailed: return QStringLiteral("LocalControlPublishFailed");
    case SecurityEventType::LocalSafetyStopPublishFailed: return QStringLiteral("LocalSafetyStopPublishFailed");
    case SecurityEventType::LocalSafetyStopUnavailable: return QStringLiteral("LocalSafetyStopUnavailable");
    case SecurityEventType::ManualIncident: return QStringLiteral("ManualIncident");
    }
    return {};
}

QString securityEventSeverityName(SecurityEventSeverity severity)
{
    switch (severity) {
    case SecurityEventSeverity::Low: return QStringLiteral("Low");
    case SecurityEventSeverity::Medium: return QStringLiteral("Medium");
    case SecurityEventSeverity::High: return QStringLiteral("High");
    case SecurityEventSeverity::Critical: return QStringLiteral("Critical");
    }
    return {};
}

QString securityEventStateName(SecurityEventState state)
{
    switch (state) {
    case SecurityEventState::Open: return QStringLiteral("Open");
    case SecurityEventState::Acknowledged: return QStringLiteral("Acknowledged");
    case SecurityEventState::Resolved: return QStringLiteral("Resolved");
    case SecurityEventState::Closed: return QStringLiteral("Closed");
    }
    return {};
}

QString resolutionSourceName(ResolutionSource source)
{
    switch (source) {
    case ResolutionSource::None: return {};
    case ResolutionSource::PlatformObservation: return QStringLiteral("platform-observation");
    case ResolutionSource::Operator: return QStringLiteral("operator");
    }
    return {};
}

QString closeDispositionName(CloseDisposition disposition)
{
    switch (disposition) {
    case CloseDisposition::None: return {};
    case CloseDisposition::ObservedRecovery: return QStringLiteral("observed-recovery");
    case CloseDisposition::ClosedWithoutObservedRecovery:
        return QStringLiteral("closed_without_observed_recovery");
    }
    return {};
}

QString eventTransitionKindName(EventTransitionKind transition)
{
    switch (transition) {
    case EventTransitionKind::Created: return QStringLiteral("created");
    case EventTransitionKind::Acknowledged: return QStringLiteral("acknowledged");
    case EventTransitionKind::Recurred: return QStringLiteral("recurred");
    case EventTransitionKind::Recovered: return QStringLiteral("recovered");
    case EventTransitionKind::ResolvedByOperator: return QStringLiteral("resolved-by-operator");
    case EventTransitionKind::Closed: return QStringLiteral("closed");
    case EventTransitionKind::ClosedWithoutObservedRecovery:
        return QStringLiteral("closed-without-observed-recovery");
    }
    return {};
}

std::optional<SecurityEventType> securityEventTypeFromName(const QString &name)
{
    return parseNamedEnum<SecurityEventType>(name, {
        {SecurityEventType::MqttConnectionLost, "MqttConnectionLost"},
        {SecurityEventType::DevicePresenceLost, "DevicePresenceLost"},
        {SecurityEventType::VideoStreamLost, "VideoStreamLost"},
        {SecurityEventType::MediaServerUnhealthy, "MediaServerUnhealthy"},
        {SecurityEventType::LocalControlPublishFailed, "LocalControlPublishFailed"},
        {SecurityEventType::LocalSafetyStopPublishFailed, "LocalSafetyStopPublishFailed"},
        {SecurityEventType::LocalSafetyStopUnavailable, "LocalSafetyStopUnavailable"},
        {SecurityEventType::ManualIncident, "ManualIncident"},
    });
}

std::optional<SecurityEventSeverity> securityEventSeverityFromName(const QString &name)
{
    return parseNamedEnum<SecurityEventSeverity>(name, {
        {SecurityEventSeverity::Low, "Low"}, {SecurityEventSeverity::Medium, "Medium"},
        {SecurityEventSeverity::High, "High"}, {SecurityEventSeverity::Critical, "Critical"},
    });
}

std::optional<SecurityEventState> securityEventStateFromName(const QString &name)
{
    return parseNamedEnum<SecurityEventState>(name, {
        {SecurityEventState::Open, "Open"},
        {SecurityEventState::Acknowledged, "Acknowledged"},
        {SecurityEventState::Resolved, "Resolved"},
        {SecurityEventState::Closed, "Closed"},
    });
}

std::optional<ResolutionSource> resolutionSourceFromName(const QString &name)
{
    if (name.isEmpty()) return ResolutionSource::None;
    return parseNamedEnum<ResolutionSource>(name, {
        {ResolutionSource::PlatformObservation, "platform-observation"},
        {ResolutionSource::Operator, "operator"},
    });
}

std::optional<CloseDisposition> closeDispositionFromName(const QString &name)
{
    if (name.isEmpty()) return CloseDisposition::None;
    return parseNamedEnum<CloseDisposition>(name, {
        {CloseDisposition::ObservedRecovery, "observed-recovery"},
        {CloseDisposition::ClosedWithoutObservedRecovery, "closed_without_observed_recovery"},
    });
}

std::optional<EventTransitionKind> eventTransitionKindFromName(const QString &name)
{
    return parseNamedEnum<EventTransitionKind>(name, {
        {EventTransitionKind::Created, "created"},
        {EventTransitionKind::Acknowledged, "acknowledged"},
        {EventTransitionKind::Recurred, "recurred"},
        {EventTransitionKind::Recovered, "recovered"},
        {EventTransitionKind::ResolvedByOperator, "resolved-by-operator"},
        {EventTransitionKind::Closed, "closed"},
        {EventTransitionKind::ClosedWithoutObservedRecovery, "closed-without-observed-recovery"},
    });
}
