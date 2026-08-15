#pragma once

#include <QtGlobal>

enum class ControlSessionState {
    Locked,
    Armed,
    Moving,
    Suspended,
};

enum class ControlIntentKind {
    StartStream,
    StopStream,
    Move,
    StopCar,
};

enum class ControlAttemptOutcome {
    Rejected,
    Submitted,
    PublishFailed,
};

enum class ControlDecisionReason {
    None,
    TargetMissing,
    MqttDisconnected,
    DeviceNotOnline,
    ControlLocked,
    PlaybackNotPlaying,
    FrameUnavailable,
    FrameStale,
};

enum class ControlInvalidationCause {
    TargetChanged,
    HeartbeatTimeout,
    MqttDisconnected,
    PlaybackInterrupted,
    FrameStale,
    FocusLost,
    FullscreenTransition,
    ApplicationExit,
    ExplicitLock,
};

struct ControlContext
{
    static constexpr qint64 kMaximumPresentedFrameAgeMs = 1'000;

    bool hasTarget = false;
    bool mqttConnected = false;
    bool heartbeatOnline = false;
    bool playbackPlaying = false;
    qint64 presentedFrameAgeMs = -1;
};

struct ControlDecision
{
    bool allowed = false;
    ControlDecisionReason reason = ControlDecisionReason::None;
};

struct ControlInvalidationResult
{
    bool stateChanged = false;
    bool shouldAttemptStop = false;
};

/** Pure, owner-thread policy for the local vehicle-control session. */
class ControlSessionGuard final
{
public:
    [[nodiscard]] ControlSessionState state() const noexcept;
    [[nodiscard]] ControlDecision requestArm(const ControlContext &context);
    [[nodiscard]] bool requestLock() noexcept;
    [[nodiscard]] ControlDecision decide(
        ControlIntentKind intent,
        const ControlContext &context
    ) const noexcept;
    void applyOutcome(
        ControlIntentKind intent,
        ControlAttemptOutcome outcome
    ) noexcept;
    [[nodiscard]] ControlInvalidationResult invalidate(
        ControlInvalidationCause cause
    ) noexcept;
    [[nodiscard]] bool conditionsRestored(const ControlContext &context);
    void forceLocked() noexcept;

private:
    [[nodiscard]] static ControlDecision movementAvailability(
        const ControlContext &context
    ) noexcept;

    ControlSessionState state_ = ControlSessionState::Locked;
};
