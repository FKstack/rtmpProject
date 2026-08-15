#include "app/PlatformEventBridge.h"

#include <QCryptographicHash>
#include <QDir>
#include <QUrl>

#include <algorithm>

#include "app/DeviceControlController.h"
#include "event_center/EventCenterService.h"

namespace {

bool isMovement(DeviceCommand command)
{
    return command == DeviceCommand::MoveForward ||
           command == DeviceCommand::MoveBackward ||
           command == DeviceCommand::TurnLeft ||
           command == DeviceCommand::TurnRight;
}

QString commandName(DeviceCommand command)
{
    switch (command) {
    case DeviceCommand::StartStream: return QStringLiteral("START_STREAM");
    case DeviceCommand::StopStream: return QStringLiteral("STOP_STREAM");
    case DeviceCommand::MoveForward: return QStringLiteral("MOVE_FORWARD");
    case DeviceCommand::MoveBackward: return QStringLiteral("MOVE_BACKWARD");
    case DeviceCommand::TurnLeft: return QStringLiteral("TURN_LEFT");
    case DeviceCommand::TurnRight: return QStringLiteral("TURN_RIGHT");
    case DeviceCommand::StopCar: return QStringLiteral("STOP_CAR");
    }
    return QStringLiteral("UNKNOWN");
}

QString outcomeName(ControlAttemptOutcome outcome)
{
    switch (outcome) {
    case ControlAttemptOutcome::Rejected: return QStringLiteral("rejected");
    case ControlAttemptOutcome::Submitted: return QStringLiteral("submitted");
    case ControlAttemptOutcome::PublishFailed: return QStringLiteral("publish_failed");
    }
    return QStringLiteral("rejected");
}

QString sourceName(ControlAttemptSource source)
{
    switch (source) {
    case ControlAttemptSource::Joystick: return QStringLiteral("joystick");
    case ControlAttemptSource::Keyboard: return QStringLiteral("keyboard");
    case ControlAttemptSource::Button: return QStringLiteral("button");
    case ControlAttemptSource::FocusLost: return QStringLiteral("focus_lost");
    case ControlAttemptSource::TargetChanged: return QStringLiteral("target_changed");
    case ControlAttemptSource::HeartbeatTimeout: return QStringLiteral("heartbeat_timeout");
    case ControlAttemptSource::MqttDisconnected: return QStringLiteral("mqtt_disconnected");
    case ControlAttemptSource::PlaybackInterrupted: return QStringLiteral("playback_interrupted");
    case ControlAttemptSource::FrameStale: return QStringLiteral("frame_stale");
    case ControlAttemptSource::FullscreenTransition: return QStringLiteral("fullscreen_transition");
    case ControlAttemptSource::ApplicationExit: return QStringLiteral("application_exit");
    }
    return QStringLiteral("button");
}

QString normalizedEndpoint(const MediaServerEndpoint &endpoint)
{
    QUrl api = endpoint.apiBaseUrl;
    api.setUserInfo({});
    api.setQuery(QString());
    api.setFragment({});
    api.setScheme(api.scheme().toLower());
    api.setHost(api.host().toLower());
    api.setPath(QDir::cleanPath(api.path()));
    return endpoint.host.trimmed().toLower() + QLatin1Char(':') +
           QString::number(endpoint.rtmpPort) + QLatin1Char('|') +
           endpoint.application.trimmed() + QLatin1Char('|') +
           api.toString(QUrl::FullyEncoded);
}

} // namespace

PlatformEventBridge::PlatformEventBridge(EventCenterService *service,
                                         QObject *parent)
    : QObject(parent), service_(service)
{
    Q_ASSERT(service_ != nullptr);
}

void PlatformEventBridge::setMediaServerEndpoint(
    const MediaServerEndpoint &endpoint)
{
    const QByteArray digest = QCryptographicHash::hash(
        normalizedEndpoint(endpoint).toUtf8(), QCryptographicHash::Sha256).toHex();
    mediaServerResourceId_ = QStringLiteral("media-server:") +
                             QString::fromLatin1(digest);
    publishResources();
}

void PlatformEventBridge::observeMqttState(MqttConnectionState state)
{
    if (!accepting_ || shutdownMode_) return;
    EventObservation observation;
    observation.eventType = SecurityEventType::MqttConnectionLost;
    observation.severity = movementPotential_ ? SecurityEventSeverity::High
                                              : SecurityEventSeverity::Medium;
    observation.localResourceId = QStringLiteral("transport:mqtt-control");
    observation.displayNameSnapshot = tr("MQTT 控制通道");
    observation.identitySource = QStringLiteral("local");
    observation.source = QStringLiteral("mqtt-connection-state");
    if (state == MqttConnectionState::Connected) {
        mqttSeenConnected_ = true;
        mqttFaultOpen_ = false;
        submitRecovery(observation);
    } else if (mqttSeenConnected_ && !mqttFaultOpen_ &&
               state != MqttConnectionState::Connecting &&
               state != MqttConnectionState::Subscribing) {
        mqttFaultOpen_ = true;
        submitFault(observation);
    }
}

void PlatformEventBridge::observeDeviceBound(const QString &deviceId)
{
    const QString normalized = deviceId.trimmed();
    if (normalized.isEmpty()) return;
    registeredDevices_.insert(normalized);
    publishResources();
}

void PlatformEventBridge::observeDeviceUnbound(const QString &deviceId)
{
    const QString normalized = deviceId.trimmed();
    registeredDevices_.remove(normalized);
    devicesSeenOnline_.remove(normalized);
    deviceFaults_.remove(normalized);
    publishResources();
}

void PlatformEventBridge::observePresence(const QString &deviceId,
                                          DevicePresenceState state)
{
    if (!accepting_ || shutdownMode_) return;
    const QString normalized = deviceId.trimmed();
    if (normalized.isEmpty() || !registeredDevices_.contains(normalized)) return;
    EventObservation observation;
    observation.eventType = SecurityEventType::DevicePresenceLost;
    observation.severity = SecurityEventSeverity::Medium;
    observation.localResourceId = QStringLiteral("device:") + normalized;
    observation.deviceId = normalized;
    observation.displayNameSnapshot = normalized;
    observation.source = QStringLiteral("device-heartbeat");
    if (state == DevicePresenceState::Online) {
        devicesSeenOnline_.insert(normalized);
        deviceFaults_.remove(normalized);
        submitRecovery(observation);
    } else if (state == DevicePresenceState::Offline &&
               devicesSeenOnline_.contains(normalized) &&
               !deviceFaults_.contains(normalized)) {
        deviceFaults_.insert(normalized);
        submitFault(observation);
    }
}

void PlatformEventBridge::observeStream(
    const StreamEventObservation &observation)
{
    if (!accepting_ || shutdownMode_ || observation.removing ||
        observation.streamId == kInvalidStreamId ||
        observation.localResourceId.isEmpty()) return;
    StreamState &state = streams_[observation.streamId];
    state.observation = observation;
    EventObservation event;
    event.eventType = SecurityEventType::VideoStreamLost;
    event.severity = SecurityEventSeverity::Medium;
    event.localResourceId = observation.localResourceId;
    event.deviceId = observation.deviceId;
    event.displayNameSnapshot = observation.displayName;
    event.source = QStringLiteral("rtmp-playback-state");
    if (observation.playbackStatus == DeviceStatus::Playing) {
        state.seenPlaying = true;
        state.faultOpen = false;
        submitRecovery(event);
    } else if (state.seenPlaying && !state.faultOpen &&
               (observation.playbackStatus == DeviceStatus::Error ||
                observation.playbackStatus == DeviceStatus::Reconnecting ||
                observation.playbackStatus == DeviceStatus::Disconnected)) {
        state.faultOpen = true;
        submitFault(event);
    }
    publishResources();
}

void PlatformEventBridge::observeStreamRemoved(
    const StreamEventObservation &observation)
{
    streams_.remove(observation.streamId);
    publishResources();
}

void PlatformEventBridge::observeMediaHealth(const MediaServerHealth &health)
{
    if (!accepting_ || shutdownMode_ || mediaServerResourceId_.isEmpty()) return;
    EventObservation observation;
    observation.eventType = SecurityEventType::MediaServerUnhealthy;
    observation.severity = SecurityEventSeverity::High;
    observation.localResourceId = mediaServerResourceId_;
    observation.displayNameSnapshot = tr("SRS 媒体服务器");
    observation.identitySource = QStringLiteral("endpoint-derived");
    observation.source = QStringLiteral("srs-health");
    if (health.state == MediaServerState::Healthy) {
        mediaServerSeenHealthy_ = true;
        mediaServerFaultOpen_ = false;
        submitRecovery(observation);
    } else if (mediaServerSeenHealthy_ && !mediaServerFaultOpen_ &&
               (health.state == MediaServerState::Degraded ||
                health.state == MediaServerState::Unavailable)) {
        mediaServerFaultOpen_ = true;
        submitFault(observation);
    }
}

void PlatformEventBridge::observeControlAttempt(
    const ControlAttemptSnapshot &attempt)
{
    if (!accepting_) return;
    if (isMovement(attempt.command) &&
        attempt.localOutcome == ControlAttemptOutcome::Submitted) {
        movementPotential_ = true;
    }
    if (attempt.command == DeviceCommand::StopCar &&
        attempt.localOutcome == ControlAttemptOutcome::Submitted) {
        movementPotential_ = false;
    }
    const QString resource = controlResourceId(attempt);
    if (resource.isEmpty()) return;
    EventObservation observation;
    observation.localResourceId = resource;
    observation.deviceId = attempt.targetDeviceId;
    observation.displayNameSnapshot = attempt.targetDeviceId;
    observation.source = QStringLiteral("local-control-attempt");
    observation.controlAttempt = controlSummary(attempt);
    if (attempt.command == DeviceCommand::StopCar) {
        observation.severity = SecurityEventSeverity::Critical;
        if (attempt.localOutcome == ControlAttemptOutcome::PublishFailed) {
            observation.eventType = SecurityEventType::LocalSafetyStopPublishFailed;
            submitFault(observation);
        } else if (attempt.localOutcome == ControlAttemptOutcome::Rejected &&
                   attempt.reason == ControlDecisionReason::MqttDisconnected) {
            observation.eventType = SecurityEventType::LocalSafetyStopUnavailable;
            submitFault(observation);
        } else if (attempt.localOutcome == ControlAttemptOutcome::Submitted) {
            observation.eventType = SecurityEventType::LocalSafetyStopPublishFailed;
            submitRecovery(observation);
            observation.eventType = SecurityEventType::LocalSafetyStopUnavailable;
            submitRecovery(observation);
        }
        return;
    }
    observation.eventType = SecurityEventType::LocalControlPublishFailed;
    observation.severity = SecurityEventSeverity::Medium;
    if (attempt.localOutcome == ControlAttemptOutcome::PublishFailed)
        submitFault(observation);
    else if (attempt.localOutcome == ControlAttemptOutcome::Submitted)
        submitRecovery(observation);
}

void PlatformEventBridge::beginShutdown()
{
    shutdownMode_ = true;
}

void PlatformEventBridge::stopAccepting()
{
    accepting_ = false;
    service_->stopAccepting();
}

QList<EventResourceDescriptor> PlatformEventBridge::resources() const
{
    QHash<QString, EventResourceDescriptor> values;
    values.insert(QStringLiteral("platform:local"), {
        QStringLiteral("platform:local"), {}, tr("本机平台"),
        QStringLiteral("local")});
    values.insert(QStringLiteral("transport:mqtt-control"), {
        QStringLiteral("transport:mqtt-control"), {}, tr("MQTT 控制通道"),
        QStringLiteral("local")});
    if (!mediaServerResourceId_.isEmpty()) {
        values.insert(mediaServerResourceId_, {
            mediaServerResourceId_, {}, tr("SRS 媒体服务器"),
            QStringLiteral("endpoint-derived")});
    }
    for (const QString &deviceId : registeredDevices_) {
        const QString resource = QStringLiteral("device:") + deviceId;
        values.insert(resource, {resource, deviceId, deviceId,
                                 QStringLiteral("url-derived")});
    }
    for (const StreamState &state : streams_) {
        const auto &item = state.observation;
        values.insert(item.localResourceId, {
            item.localResourceId, item.deviceId,
            item.displayName.isEmpty() ? item.localResourceId : item.displayName,
            QStringLiteral("url-derived")});
    }
    QList<EventResourceDescriptor> result = values.values();
    std::sort(result.begin(), result.end(),
              [](const auto &left, const auto &right) {
                  return left.displayName.localeAwareCompare(right.displayName) < 0;
              });
    return result;
}

QString PlatformEventBridge::localActorName()
{
    QString actor = qEnvironmentVariable("USERNAME").trimmed();
    if (actor.isEmpty()) actor = qEnvironmentVariable("USER").trimmed();
    if (actor.isEmpty()) actor = QStringLiteral("local-user");
    return actor;
}

QString PlatformEventBridge::controlResourceId(
    const ControlAttemptSnapshot &attempt) const
{
    if (!attempt.targetDeviceId.trimmed().isEmpty())
        return QStringLiteral("device:") + attempt.targetDeviceId.trimmed();
    const auto found = streams_.constFind(attempt.targetStreamId);
    return found == streams_.cend() ? QString()
                                    : found->observation.localResourceId;
}

EventControlAttemptSummary PlatformEventBridge::controlSummary(
    const ControlAttemptSnapshot &attempt)
{
    return {
        attempt.attemptId,
        attempt.observedAtUtc.isValid() ? attempt.observedAtUtc.toUTC()
                                        : QDateTime::currentDateTimeUtc(),
        commandName(attempt.command),
        outcomeName(attempt.localOutcome),
        sourceName(attempt.source),
        QStringLiteral("unavailable"),
        attempt.targetDeviceId,
        QStringLiteral("url-derived"),
    };
}

void PlatformEventBridge::submitFault(const EventObservation &observation)
{
    const EventOperationResult result = service_->observeFault(observation);
    if (!result.succeeded()) qWarning().noquote() << result.message;
}

void PlatformEventBridge::submitRecovery(const EventObservation &observation)
{
    const EventOperationResult result = service_->observeRecovery(observation);
    if (!result.succeeded()) qWarning().noquote() << result.message;
}

void PlatformEventBridge::publishResources()
{
    emit resourcesChanged(resources());
}
