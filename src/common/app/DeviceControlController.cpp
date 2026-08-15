#include "app/DeviceControlController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonObject>
#include <QMessageBox>
#include <QUuid>

#include <utility>

#include "app/DeviceControlTransport.h"
#include "device_control/DeviceCommandCodec.h"
#include "device_control/DeviceHeartbeatCodec.h"
#include "device_control/DevicePresenceTracker.h"
#include "logging/LogManager.h"
#include "ui/DeviceControlPanel.h"
#include "ui/MainWindow.h"
#include "ui/MqttSettingsDialog.h"

namespace {

bool isMovement(DeviceCommand command)
{
    return command == DeviceCommand::MoveForward ||
           command == DeviceCommand::MoveBackward ||
           command == DeviceCommand::TurnLeft ||
           command == DeviceCommand::TurnRight;
}

ControlIntentKind intentFor(DeviceCommand command)
{
    if (isMovement(command)) return ControlIntentKind::Move;
    switch (command) {
    case DeviceCommand::StartStream: return ControlIntentKind::StartStream;
    case DeviceCommand::StopStream: return ControlIntentKind::StopStream;
    case DeviceCommand::StopCar: return ControlIntentKind::StopCar;
    default: return ControlIntentKind::Move;
    }
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
    case ControlAttemptOutcome::PublishFailed:
        return QStringLiteral("publish_failed");
    }
    return QStringLiteral("rejected");
}

QString reasonName(ControlDecisionReason reason)
{
    switch (reason) {
    case ControlDecisionReason::None: return QStringLiteral("none");
    case ControlDecisionReason::TargetMissing:
        return QStringLiteral("target_missing");
    case ControlDecisionReason::MqttDisconnected:
        return QStringLiteral("mqtt_disconnected");
    case ControlDecisionReason::DeviceNotOnline:
        return QStringLiteral("device_not_online");
    case ControlDecisionReason::ControlLocked:
        return QStringLiteral("control_locked");
    case ControlDecisionReason::PlaybackNotPlaying:
        return QStringLiteral("playback_not_playing");
    case ControlDecisionReason::FrameUnavailable:
        return QStringLiteral("frame_unavailable");
    case ControlDecisionReason::FrameStale:
        return QStringLiteral("frame_stale");
    }
    return QStringLiteral("unknown");
}

QString sourceName(ControlAttemptSource source)
{
    switch (source) {
    case ControlAttemptSource::Joystick: return QStringLiteral("joystick");
    case ControlAttemptSource::Keyboard: return QStringLiteral("keyboard");
    case ControlAttemptSource::Button: return QStringLiteral("button");
    case ControlAttemptSource::FocusLost: return QStringLiteral("focus_lost");
    case ControlAttemptSource::TargetChanged:
        return QStringLiteral("target_changed");
    case ControlAttemptSource::HeartbeatTimeout:
        return QStringLiteral("heartbeat_timeout");
    case ControlAttemptSource::MqttDisconnected:
        return QStringLiteral("mqtt_disconnected");
    case ControlAttemptSource::PlaybackInterrupted:
        return QStringLiteral("playback_interrupted");
    case ControlAttemptSource::FrameStale: return QStringLiteral("frame_stale");
    case ControlAttemptSource::FullscreenTransition:
        return QStringLiteral("fullscreen_transition");
    case ControlAttemptSource::ApplicationExit:
        return QStringLiteral("application_exit");
    }
    return QStringLiteral("button");
}

QString presenceName(DevicePresenceState state)
{
    switch (state) {
    case DevicePresenceState::Unavailable: return QStringLiteral("unavailable");
    case DevicePresenceState::Waiting: return QStringLiteral("waiting");
    case DevicePresenceState::Online: return QStringLiteral("online");
    case DevicePresenceState::Offline: return QStringLiteral("offline");
    }
    return QStringLiteral("unavailable");
}

QString mqttStateName(MqttConnectionState state)
{
    switch (state) {
    case MqttConnectionState::Disabled: return QStringLiteral("disabled");
    case MqttConnectionState::Disconnected:
        return QStringLiteral("disconnected");
    case MqttConnectionState::Connecting: return QStringLiteral("connecting");
    case MqttConnectionState::Connected: return QStringLiteral("connected");
    case MqttConnectionState::Reconnecting:
        return QStringLiteral("reconnecting");
    case MqttConnectionState::Error: return QStringLiteral("error");
    case MqttConnectionState::Subscribing:
        return QStringLiteral("subscribing");
    }
    return QStringLiteral("disconnected");
}

QString localActor()
{
    QString actor = qEnvironmentVariable("USERNAME").trimmed();
    if (actor.isEmpty()) actor = qEnvironmentVariable("USER").trimmed();
    return actor.isEmpty() ? QStringLiteral("local-user") : actor;
}

QString rejectionText(ControlDecisionReason reason)
{
    switch (reason) {
    case ControlDecisionReason::TargetMissing:
        return QObject::tr("请先选择控制目标，未发送指令。");
    case ControlDecisionReason::MqttDisconnected:
        return QObject::tr("MQTT 当前不可用，本地未提交指令。");
    case ControlDecisionReason::DeviceNotOnline:
        return QObject::tr("所选设备当前没有有效心跳，未发送指令。");
    case ControlDecisionReason::ControlLocked:
        return QObject::tr("车辆移动尚未解锁，未发送指令。");
    case ControlDecisionReason::PlaybackNotPlaying:
        return QObject::tr("所选视频当前未播放，车辆移动已拒绝。");
    case ControlDecisionReason::FrameUnavailable:
        return QObject::tr("尚无已呈现画面，车辆移动已拒绝。");
    case ControlDecisionReason::FrameStale:
        return QObject::tr("最近画面已超过 1 秒，车辆移动已拒绝。");
    case ControlDecisionReason::None: break;
    }
    return QObject::tr("本地安全策略拒绝了该指令。");
}

AuditResult auditResult(ControlAttemptOutcome outcome)
{
    switch (outcome) {
    case ControlAttemptOutcome::Submitted: return AuditResult::Submitted;
    case ControlAttemptOutcome::Rejected: return AuditResult::Rejected;
    case ControlAttemptOutcome::PublishFailed:
        return AuditResult::PublishFailed;
    }
    return AuditResult::Failure;
}

} // namespace

DeviceControlController::DeviceControlController(
    MainWindow *mainWindow,
    DeviceControlPanel *panel,
    DeviceControlTransport *transport,
    DevicePresenceTracker *presenceTracker,
    LogManager *logManager,
    MediaObservationProvider mediaObservationProvider,
    MqttSettingsRepository repository,
    QObject *parent
)
    : QObject(parent)
    , mainWindow_(mainWindow)
    , panel_(panel)
    , transport_(transport)
    , presenceTracker_(presenceTracker)
    , logManager_(logManager)
    , mediaObservationProvider_(std::move(mediaObservationProvider))
    , repository_(std::move(repository))
{
    Q_ASSERT(mainWindow_ && panel_ && transport_ && presenceTracker_ &&
             logManager_ && mediaObservationProvider_);
    qRegisterMetaType<ControlAttemptSnapshot>();

    connect(panel_, &DeviceControlPanel::settingsRequested,
            this, &DeviceControlController::showSettings);

    connect(transport_, &DeviceControlTransport::stateChanged, this,
            &DeviceControlController::handleTransportState);
    connect(transport_, &DeviceControlTransport::commandFailed, this,
            [this](DeviceCommand, const QString &detail) {
                logManager_->logSystem(LogLevel::Warning,
                    QStringLiteral("mqtt"), QStringLiteral("command_failed"),
                    detail);
            });
    connect(transport_, &DeviceControlTransport::messageReceived, this,
            &DeviceControlController::handleObservedMessage);
    connect(transport_, &DeviceControlTransport::observedMessagesDropped, this,
            [this](quint64 count) {
                panel_->showObservedMessagesDropped(count);
                logManager_->logSystem(
                    LogLevel::Warning, QStringLiteral("mqtt"),
                    QStringLiteral("observed_messages_dropped"),
                    QStringLiteral("MQTT observed message inbox overflowed."),
                    {{QStringLiteral("droppedCount"),
                      static_cast<qint64>(count)}});
            });

    availabilityTimer_.setInterval(100);
    availabilityTimer_.setTimerType(Qt::PreciseTimer);
    connect(&availabilityTimer_, &QTimer::timeout,
            this, &DeviceControlController::refreshControlAvailability);
    connect(qApp, &QCoreApplication::aboutToQuit,
            this, &DeviceControlController::stop);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) {
                    invalidateControl(ControlInvalidationCause::FocusLost,
                                      ControlAttemptSource::FocusLost);
                }
            });
    publishSessionPresentation();
}

DeviceControlController::~DeviceControlController()
{
    stop();
}

void DeviceControlController::start()
{
    if (started_) return;
    started_ = true;
    stopping_ = false;
    const MqttSettingsLoadResult loaded = repository_.load();
    options_ = loaded.ok() ? loaded.options : MqttConnectionOptions {};
    if (!loaded.ok()) {
        options_.enabled = false;
        panel_->setLastResult(loaded.error, true);
        logManager_->logSystem(LogLevel::Warning, QStringLiteral("mqtt"),
            QStringLiteral("settings_load_failed"), loaded.error);
    }
    panel_->setTopics(options_.topic, options_.statusTopic);
    availabilityTimer_.start();
    transport_->connectToBroker(options_);
}

void DeviceControlController::stop()
{
    if (stopping_) return;
    availabilityTimer_.stop();
    invalidateControl(ControlInvalidationCause::ApplicationExit,
                      ControlAttemptSource::ApplicationExit);
    stopping_ = true;
    started_ = false;
    guard_.forceLocked();
    publishSessionPresentation();
    transport_->disconnectFromBroker();
}

ControlMediaObservation DeviceControlController::mediaObservation() const
{
    if (targetStreamId_ == kInvalidStreamId || !mediaObservationProvider_) {
        return {};
    }
    return mediaObservationProvider_(targetStreamId_);
}

ControlContext DeviceControlController::controlContext() const
{
    const ControlMediaObservation media = mediaObservation();
    return {
        targetStreamId_ != kInvalidStreamId && !targetDeviceId_.isEmpty(),
        mqttState_ == MqttConnectionState::Connected,
        targetPresence_ == DevicePresenceState::Online,
        media.playbackPlaying,
        media.presentedFrameAgeMs,
    };
}

DeviceControlController::TargetSnapshot
DeviceControlController::targetSnapshot() const
{
    return {targetStreamId_, targetDeviceId_, targetStreamUrl_, targetPresence_};
}

void DeviceControlController::publishSessionPresentation(const QString &detail)
{
    const ControlSessionState state = guard_.state();
    const bool armed = state == ControlSessionState::Armed ||
                       state == ControlSessionState::Moving;
    const bool suspended = state == ControlSessionState::Suspended;
    ControlSessionGuard probe;
    const bool armAvailable = probe.requestArm(controlContext()).allowed;
    panel_->setMovementArmAvailable(armAvailable);
    panel_->setControlSessionState(armed, suspended, detail);
    emit controlSessionChanged(armed, suspended, detail);
}

void DeviceControlController::setControlArmed(bool armed)
{
    if (armed) {
        const ControlDecision decision = guard_.requestArm(controlContext());
        if (!decision.allowed) {
            panel_->setLastResult(rejectionText(decision.reason), true);
            recordSessionTransition(reasonName(decision.reason),
                                    AuditResult::Rejected);
        } else {
            panel_->setLastResult(
                tr("车辆移动已在本机解锁；仍未确认设备可执行命令。"));
            recordSessionTransition(QStringLiteral("explicit_arm"),
                                    AuditResult::Success);
        }
        publishSessionPresentation(rejectionText(decision.reason));
        return;
    }

    const TargetSnapshot oldTarget = targetSnapshot();
    const bool shouldStop = guard_.requestLock();
    emit interactiveControlRevoked();
    if (shouldStop) {
        attemptSafetyStop(oldTarget, ControlAttemptSource::Button);
    }
    recordSessionTransition(QStringLiteral("explicit_lock"),
                            AuditResult::Success);
    publishSessionPresentation();
}

void DeviceControlController::submitCommand(
    DeviceCommand command,
    ControlAttemptSource source
)
{
    submitForTarget(command, source, targetSnapshot());
}

void DeviceControlController::releaseMovement(ControlAttemptSource source)
{
    if (guard_.state() == ControlSessionState::Moving) {
        submitForTarget(DeviceCommand::StopCar, source, targetSnapshot());
    }
}

void DeviceControlController::submitForTarget(
    DeviceCommand command,
    ControlAttemptSource source,
    const TargetSnapshot &target
)
{
    ControlContext context = controlContext();
    context.hasTarget = target.streamId != kInvalidStreamId &&
                        !target.deviceId.isEmpty();
    context.heartbeatOnline = target.presence == DevicePresenceState::Online;
    const ControlIntentKind intent = intentFor(command);
    const ControlDecision decision = guard_.decide(intent, context);

    ControlAttemptSnapshot attempt;
    attempt.command = command;
    attempt.targetStreamId = target.streamId;
    attempt.targetDeviceId = target.deviceId;
    attempt.presence = target.presence;
    attempt.mqttState = mqttState_;
    attempt.playbackPlaying = context.playbackPlaying;
    attempt.presentedFrameAgeMs = context.presentedFrameAgeMs;
    attempt.source = source;

    if (!decision.allowed) {
        attempt.localOutcome = ControlAttemptOutcome::Rejected;
        attempt.reason = decision.reason;
        recordAttempt(attempt);
        panel_->setLastResult(rejectionText(decision.reason), true);
        if (command == DeviceCommand::StopCar) {
            pendingSafetyStop_ = PendingSafetyStop {target, source};
            logManager_->logSystem(
                LogLevel::Critical, QStringLiteral("device_control"),
                QStringLiteral("local_safety_stop_unavailable"),
                QStringLiteral(
                    "Local safety stop could not be submitted; device state is unknown."
                ),
                {{QStringLiteral("targetStreamId"),
                  QString::number(target.streamId)},
                 {QStringLiteral("targetDeviceId"), target.deviceId},
                 {QStringLiteral("executionConfirmation"),
                  QStringLiteral("unavailable")}});
        }
        publishSessionPresentation(rejectionText(decision.reason));
        return;
    }

    const bool submitted = command == DeviceCommand::StartStream
        ? transport_->publishStartStream(target.streamUrl)
        : transport_->publish(command);
    attempt.localOutcome = submitted
        ? ControlAttemptOutcome::Submitted
        : ControlAttemptOutcome::PublishFailed;
    attempt.reason = submitted ? ControlDecisionReason::None
                               : ControlDecisionReason::MqttDisconnected;
    guard_.applyOutcome(intent, attempt.localOutcome);
    recordAttempt(attempt);

    if (command == DeviceCommand::StopCar) {
        if (submitted) {
            pendingSafetyStop_.reset();
            panel_->setLastResult(
                tr("停车指令已尝试发送；未确认车辆已停车。"));
        } else {
            pendingSafetyStop_ = PendingSafetyStop {target, source};
            panel_->setLastResult(
                tr("本地发送失败，设备状态未知。"), true);
            logManager_->logSystem(
                LogLevel::Critical, QStringLiteral("device_control"),
                QStringLiteral("local_safety_stop_publish_failed"),
                QStringLiteral(
                    "Local safety stop publish failed; device state is unknown."
                ),
                {{QStringLiteral("targetStreamId"),
                  QString::number(target.streamId)},
                 {QStringLiteral("targetDeviceId"), target.deviceId},
                 {QStringLiteral("executionConfirmation"),
                  QStringLiteral("unavailable")}});
        }
    } else if (submitted) {
        panel_->setLastResult(
            tr("本地发送请求已提交；未确认 Broker 或设备接收。"));
    } else {
        panel_->setLastResult(tr("本地发送失败，设备状态未知。"), true);
    }
    publishSessionPresentation();
}

void DeviceControlController::recordAttempt(ControlAttemptSnapshot attempt)
{
    attempt.attemptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    emit controlAttemptRecorded(attempt);

    AuditRecord audit;
    audit.actor = localActor();
    audit.action = AuditAction::ControlCommandAttempt;
    audit.targetType = QStringLiteral("device-control-target");
    audit.targetId = attempt.targetDeviceId;
    audit.result = auditResult(attempt.localOutcome);
    audit.reason = reasonName(attempt.reason);
    audit.source = sourceName(attempt.source);
    audit.afterValues = {
        {QStringLiteral("attemptId"), attempt.attemptId},
        {QStringLiteral("command"), commandName(attempt.command)},
        {QStringLiteral("targetStreamId"),
         QString::number(attempt.targetStreamId)},
        {QStringLiteral("targetDeviceId"), attempt.targetDeviceId},
        {QStringLiteral("identitySource"), QStringLiteral("url-derived")},
        {QStringLiteral("presence"), presenceName(attempt.presence)},
        {QStringLiteral("mqttState"), mqttStateName(attempt.mqttState)},
        {QStringLiteral("playbackState"),
         attempt.playbackPlaying ? QStringLiteral("playing")
                                 : QStringLiteral("not_playing")},
        {QStringLiteral("presentedFrameAgeMs"),
         attempt.presentedFrameAgeMs},
        {QStringLiteral("localOutcome"), outcomeName(attempt.localOutcome)},
        {QStringLiteral("executionConfirmation"),
         QStringLiteral("unavailable")},
        {QStringLiteral("actorAssurance"),
         QStringLiteral("unverified-local")},
        {QStringLiteral("reasonCode"), reasonName(attempt.reason)},
        {QStringLiteral("source"), sourceName(attempt.source)},
    };
    logManager_->logAudit(audit);
}

void DeviceControlController::recordSessionTransition(
    const QString &reason,
    AuditResult result
)
{
    AuditRecord audit;
    audit.actor = localActor();
    audit.action = AuditAction::ControlSessionTransition;
    audit.targetType = QStringLiteral("device-control-session");
    audit.targetId = targetDeviceId_;
    audit.result = result;
    audit.reason = reason;
    audit.source = QStringLiteral("local-ui");
    audit.afterValues = {
        {QStringLiteral("state"), static_cast<int>(guard_.state())},
        {QStringLiteral("actorAssurance"),
         QStringLiteral("unverified-local")},
        {QStringLiteral("executionConfirmation"),
         QStringLiteral("unavailable")},
    };
    logManager_->logAudit(audit);
}

void DeviceControlController::attemptSafetyStop(
    const TargetSnapshot &target,
    ControlAttemptSource source
)
{
    submitForTarget(DeviceCommand::StopCar, source, target);
}

void DeviceControlController::invalidateControl(
    ControlInvalidationCause cause,
    ControlAttemptSource source
)
{
    const TargetSnapshot oldTarget = targetSnapshot();
    const ControlInvalidationResult result = guard_.invalidate(cause);
    if (!result.stateChanged) return;
    emit interactiveControlRevoked();
    recordSessionTransition(sourceName(source), AuditResult::Success);
    publishSessionPresentation();
    if (result.shouldAttemptStop) attemptSafetyStop(oldTarget, source);
}

void DeviceControlController::refreshControlAvailability()
{
    const ControlContext context = controlContext();
    if (guard_.state() == ControlSessionState::Suspended) {
        if (guard_.conditionsRestored(context)) {
            recordSessionTransition(QStringLiteral("conditions_restored"),
                                    AuditResult::Success);
            publishSessionPresentation();
        }
        return;
    }
    if (guard_.state() != ControlSessionState::Armed &&
        guard_.state() != ControlSessionState::Moving) {
        publishSessionPresentation();
        return;
    }
    const ControlDecision decision = guard_.decide(ControlIntentKind::Move,
                                                    context);
    if (decision.allowed) return;

    ControlInvalidationCause cause = ControlInvalidationCause::FrameStale;
    ControlAttemptSource source = ControlAttemptSource::FrameStale;
    if (decision.reason == ControlDecisionReason::MqttDisconnected) {
        cause = ControlInvalidationCause::MqttDisconnected;
        source = ControlAttemptSource::MqttDisconnected;
    } else if (decision.reason == ControlDecisionReason::DeviceNotOnline) {
        cause = ControlInvalidationCause::HeartbeatTimeout;
        source = ControlAttemptSource::HeartbeatTimeout;
    } else if (decision.reason == ControlDecisionReason::PlaybackNotPlaying) {
        cause = ControlInvalidationCause::PlaybackInterrupted;
        source = ControlAttemptSource::PlaybackInterrupted;
    }
    invalidateControl(cause, source);
}

void DeviceControlController::setControlTarget(
    StreamId streamId,
    const QString &deviceId,
    const QString &streamUrl
)
{
    const bool changed = streamId != targetStreamId_ ||
                         deviceId.trimmed() != targetDeviceId_;
    if (!changed) return;

    const TargetSnapshot oldTarget = targetSnapshot();
    const ControlInvalidationResult invalidated = guard_.invalidate(
        ControlInvalidationCause::TargetChanged
    );
    emit interactiveControlRevoked();
    if (invalidated.shouldAttemptStop) {
        attemptSafetyStop(oldTarget, ControlAttemptSource::TargetChanged);
    }

    targetStreamId_ = streamId;
    targetDeviceId_ = deviceId.trimmed();
    targetStreamUrl_ = streamUrl.trimmed();
    targetPresence_ = targetDeviceId_.isEmpty()
        ? DevicePresenceState::Unavailable
        : presenceTracker_->state(targetDeviceId_);
    panel_->setControlTarget(targetDeviceId_, targetPresence_);
    if (guard_.state() == ControlSessionState::Suspended) {
        const bool restored = guard_.conditionsRestored(controlContext());
        Q_UNUSED(restored)
    } else {
        guard_.forceLocked();
    }
    publishSessionPresentation();
}

void DeviceControlController::setDevicePresence(
    const QString &deviceId,
    DevicePresenceState state
)
{
    if (deviceId != targetDeviceId_) return;
    targetPresence_ = state;
    panel_->setDevicePresenceState(state);
    if (state != DevicePresenceState::Online) {
        invalidateControl(ControlInvalidationCause::HeartbeatTimeout,
                          ControlAttemptSource::HeartbeatTimeout);
    } else {
        refreshControlAvailability();
    }
}

void DeviceControlController::handleTransportState(
    MqttConnectionState state,
    const QString &detail
)
{
    mqttState_ = state;
    panel_->setConnectionState(state, detail);
    if (state == MqttConnectionState::Connected) {
        presenceTracker_->setAvailable(true);
    } else if (state == MqttConnectionState::Disabled) {
        presenceTracker_->setAvailable(false);
    }
    logManager_->logSystem(
        state == MqttConnectionState::Error ? LogLevel::Warning
                                            : LogLevel::Info,
        QStringLiteral("mqtt"), QStringLiteral("connection_state_changed"),
        detail.isEmpty() ? QStringLiteral("MQTT state changed.") : detail,
        {{QStringLiteral("state"), static_cast<int>(state)}}, {},
        state == MqttConnectionState::Reconnecting);

    if (state != MqttConnectionState::Connected) {
        invalidateControl(ControlInvalidationCause::MqttDisconnected,
                          ControlAttemptSource::MqttDisconnected);
        publishSessionPresentation();
        return;
    }

    refreshControlAvailability();
    if (pendingSafetyStop_.has_value()) {
        const PendingSafetyStop pending = *pendingSafetyStop_;
        pendingSafetyStop_.reset();
        attemptSafetyStop(pending.target, pending.source);
    }
}

void DeviceControlController::showSettings()
{
    MqttSettingsDialog dialog(mainWindow_);
    dialog.setOptions(options_);
    bool testedDifferentPresenceSession = false;
    connect(transport_, &DeviceControlTransport::stateChanged, &dialog,
            [&dialog](MqttConnectionState state, const QString &detail) {
                if (state == MqttConnectionState::Connecting) {
                    dialog.setTestResult(QObject::tr("正在测试连接…"));
                } else if (state == MqttConnectionState::Subscribing) {
                    dialog.setTestResult(QObject::tr("连接成功，正在订阅 Topic…"));
                } else if (state == MqttConnectionState::Connected) {
                    dialog.setTestResult(
                        QObject::tr("连接并订阅成功（未发送设备命令）"));
                } else if (state == MqttConnectionState::Error ||
                           state == MqttConnectionState::Reconnecting) {
                    dialog.setTestResult(detail.isEmpty()
                        ? QObject::tr("连接测试失败") : detail, true);
                }
            });
    connect(&dialog, &MqttSettingsDialog::testRequested, this,
            [this, &testedDifferentPresenceSession](
                const MqttConnectionOptions &candidate) {
                MqttConnectionOptions testOptions = candidate;
                testOptions.enabled = true;
                QString error;
                if (!MqttSettingsRepository::validate(testOptions, &error)) {
                    QMessageBox::warning(mainWindow_, tr("配置无效"), error);
                    return;
                }
                invalidateControl(ControlInvalidationCause::MqttDisconnected,
                                  ControlAttemptSource::MqttDisconnected);
                const bool sessionChanged =
                    options_.brokerUrl != testOptions.brokerUrl ||
                    options_.statusTopic != testOptions.statusTopic;
                if (sessionChanged) {
                    presenceTracker_->clearSession();
                    testedDifferentPresenceSession = true;
                }
                panel_->setTopics(testOptions.topic, testOptions.statusTopic);
                transport_->connectToBroker(testOptions);
            });
    if (dialog.exec() != QDialog::Accepted) {
        if (testedDifferentPresenceSession) presenceTracker_->clearSession();
        panel_->setTopics(options_.topic, options_.statusTopic);
        transport_->connectToBroker(options_);
        return;
    }
    const MqttConnectionOptions candidate = dialog.options();
    QString error;
    invalidateControl(ControlInvalidationCause::MqttDisconnected,
                      ControlAttemptSource::MqttDisconnected);
    transport_->disconnectFromBroker();
    if (!repository_.save(candidate, &error)) {
        QMessageBox::warning(mainWindow_, tr("保存失败"), error);
        panel_->setTopics(options_.topic, options_.statusTopic);
        transport_->connectToBroker(options_);
        return;
    }
    const bool sessionChanged = options_.brokerUrl != candidate.brokerUrl ||
                                options_.statusTopic != candidate.statusTopic;
    options_ = candidate;
    if (sessionChanged) presenceTracker_->clearSession();
    panel_->setTopics(options_.topic, options_.statusTopic);
    transport_->connectToBroker(options_);
}

void DeviceControlController::handleObservedMessage(
    const MqttObservedMessage &message
)
{
    if (message.topic == options_.statusTopic) {
        QString error;
        QElapsedTimer monotonicClock;
        monotonicClock.start();
        const auto heartbeat = DeviceHeartbeatCodec::decode(
            message.payload, monotonicClock.msecsSinceReference(), &error);
        if (heartbeat.has_value()) {
            presenceTracker_->processHeartbeat(*heartbeat);
        } else {
            logManager_->logSystem(
                LogLevel::Warning, QStringLiteral("mqtt"),
                QStringLiteral("invalid_device_heartbeat"), error);
        }
    }
    MqttObservedMessage safe = message;
    if (message.topic == options_.topic) {
        safe.payload = DeviceCommandCodec::redactForDisplay(message.payload);
        safe.originalPayloadSize = safe.payload.size();
    }
    panel_->appendObservedMessage(safe);
}
