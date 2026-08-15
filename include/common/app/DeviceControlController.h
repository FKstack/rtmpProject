#pragma once

#include <QObject>
#include <QDateTime>
#include <QTimer>

#include <functional>
#include <optional>

#include "app/ControlMediaObservation.h"
#include "control_policy/ControlSessionGuard.h"
#include "device_control/DeviceControlTypes.h"
#include "device_control/MqttSettingsRepository.h"
#include "logging/LogTypes.h"
#include "media/PlaybackTypes.h"

class DeviceControlPanel;
class DeviceControlTransport;
class DevicePresenceTracker;
class LogManager;
class MainWindow;

enum class ControlAttemptSource {
    Joystick,
    Keyboard,
    Button,
    FocusLost,
    TargetChanged,
    HeartbeatTimeout,
    MqttDisconnected,
    PlaybackInterrupted,
    FrameStale,
    FullscreenTransition,
    ApplicationExit,
};

struct ControlAttemptSnapshot
{
    QString attemptId;
    QDateTime observedAtUtc;
    DeviceCommand command = DeviceCommand::StopCar;
    StreamId targetStreamId = kInvalidStreamId;
    QString targetDeviceId;
    DevicePresenceState presence = DevicePresenceState::Unavailable;
    MqttConnectionState mqttState = MqttConnectionState::Disconnected;
    bool playbackPlaying = false;
    qint64 presentedFrameAgeMs = -1;
    ControlAttemptOutcome localOutcome = ControlAttemptOutcome::Rejected;
    ControlDecisionReason reason = ControlDecisionReason::None;
    ControlAttemptSource source = ControlAttemptSource::Button;
};

Q_DECLARE_METATYPE(ControlAttemptSnapshot)

class DeviceControlController final : public QObject
{
    Q_OBJECT

public:
    using MediaObservationProvider =
        std::function<ControlMediaObservation(StreamId)>;

    DeviceControlController(
        MainWindow *mainWindow,
        DeviceControlPanel *panel,
        DeviceControlTransport *transport,
        DevicePresenceTracker *presenceTracker,
        LogManager *logManager,
        MediaObservationProvider mediaObservationProvider,
        MqttSettingsRepository repository = MqttSettingsRepository(),
        QObject *parent = nullptr
    );
    ~DeviceControlController() override;

    void start();
    void stop();
    void submitCommand(DeviceCommand command, ControlAttemptSource source);
    void releaseMovement(ControlAttemptSource source);
    void setControlArmed(bool armed);
    void invalidateControl(ControlInvalidationCause cause,
                           ControlAttemptSource source);

public slots:
    void setControlTarget(StreamId streamId, const QString &deviceId,
                          const QString &streamUrl);
    void setDevicePresence(const QString &deviceId, DevicePresenceState state);
    void refreshControlAvailability();

signals:
    void interactiveControlRevoked();
    void controlSessionChanged(bool armed, bool suspended,
                               const QString &detail);
    void controlAttemptRecorded(const ControlAttemptSnapshot &attempt);

private:
    struct TargetSnapshot
    {
        StreamId streamId = kInvalidStreamId;
        QString deviceId;
        QString streamUrl;
        DevicePresenceState presence = DevicePresenceState::Unavailable;
    };

    struct PendingSafetyStop
    {
        TargetSnapshot target;
        ControlAttemptSource source = ControlAttemptSource::Button;
    };

    [[nodiscard]] ControlContext controlContext() const;
    [[nodiscard]] ControlMediaObservation mediaObservation() const;
    [[nodiscard]] TargetSnapshot targetSnapshot() const;
    void publishSessionPresentation(const QString &detail = {});
    void attemptSafetyStop(const TargetSnapshot &target,
                           ControlAttemptSource source);
    void submitForTarget(DeviceCommand command, ControlAttemptSource source,
                         const TargetSnapshot &target);
    void recordAttempt(ControlAttemptSnapshot attempt);
    void recordSessionTransition(const QString &reason,
                                 AuditResult result);
    void showSettings();
    void handleObservedMessage(const MqttObservedMessage &message);
    void handleTransportState(MqttConnectionState state,
                              const QString &detail);

    MainWindow *mainWindow_ = nullptr;
    DeviceControlPanel *panel_ = nullptr;
    DeviceControlTransport *transport_ = nullptr;
    DevicePresenceTracker *presenceTracker_ = nullptr;
    LogManager *logManager_ = nullptr;
    MediaObservationProvider mediaObservationProvider_;
    MqttSettingsRepository repository_;
    MqttConnectionOptions options_;
    ControlSessionGuard guard_;
    QTimer availabilityTimer_;
    MqttConnectionState mqttState_ = MqttConnectionState::Disconnected;
    StreamId targetStreamId_ = kInvalidStreamId;
    QString targetDeviceId_;
    QString targetStreamUrl_;
    DevicePresenceState targetPresence_ = DevicePresenceState::Unavailable;
    std::optional<PendingSafetyStop> pendingSafetyStop_;
    bool started_ = false;
    bool stopping_ = false;
};
