#include "control_policy/ControlSessionGuard.h"

ControlSessionState ControlSessionGuard::state() const noexcept
{
    return state_;
}

ControlDecision ControlSessionGuard::movementAvailability(
    const ControlContext &context
) noexcept
{
    if (!context.hasTarget) {
        return {false, ControlDecisionReason::TargetMissing};
    }
    if (!context.mqttConnected) {
        return {false, ControlDecisionReason::MqttDisconnected};
    }
    if (!context.heartbeatOnline) {
        return {false, ControlDecisionReason::DeviceNotOnline};
    }
    if (!context.playbackPlaying) {
        return {false, ControlDecisionReason::PlaybackNotPlaying};
    }
    if (context.presentedFrameAgeMs < 0) {
        return {false, ControlDecisionReason::FrameUnavailable};
    }
    if (context.presentedFrameAgeMs >
        ControlContext::kMaximumPresentedFrameAgeMs) {
        return {false, ControlDecisionReason::FrameStale};
    }
    return {true, ControlDecisionReason::None};
}

ControlDecision ControlSessionGuard::requestArm(const ControlContext &context)
{
    if (state_ != ControlSessionState::Locked) {
        return {false, ControlDecisionReason::ControlLocked};
    }
    const ControlDecision decision = movementAvailability(context);
    if (decision.allowed) {
        state_ = ControlSessionState::Armed;
    }
    return decision;
}

bool ControlSessionGuard::requestLock() noexcept
{
    const bool shouldAttemptStop = state_ == ControlSessionState::Moving;
    state_ = ControlSessionState::Locked;
    return shouldAttemptStop;
}

ControlDecision ControlSessionGuard::decide(
    ControlIntentKind intent,
    const ControlContext &context
) const noexcept
{
    if (!context.hasTarget) {
        return {false, ControlDecisionReason::TargetMissing};
    }
    if (intent == ControlIntentKind::StopCar) {
        return context.mqttConnected
            ? ControlDecision {true, ControlDecisionReason::None}
            : ControlDecision {false, ControlDecisionReason::MqttDisconnected};
    }
    if (!context.mqttConnected) {
        return {false, ControlDecisionReason::MqttDisconnected};
    }
    if (intent == ControlIntentKind::StopStream) {
        return {true, ControlDecisionReason::None};
    }
    if (intent == ControlIntentKind::StartStream) {
        return context.heartbeatOnline
            ? ControlDecision {true, ControlDecisionReason::None}
            : ControlDecision {false, ControlDecisionReason::DeviceNotOnline};
    }
    if (state_ != ControlSessionState::Armed &&
        state_ != ControlSessionState::Moving) {
        return {false, ControlDecisionReason::ControlLocked};
    }
    return movementAvailability(context);
}

void ControlSessionGuard::applyOutcome(
    ControlIntentKind intent,
    ControlAttemptOutcome outcome
) noexcept
{
    if (intent == ControlIntentKind::Move &&
        outcome == ControlAttemptOutcome::Submitted) {
        state_ = ControlSessionState::Moving;
        return;
    }
    if (intent != ControlIntentKind::StopCar ||
        state_ != ControlSessionState::Moving) {
        return;
    }
    state_ = outcome == ControlAttemptOutcome::Submitted
        ? ControlSessionState::Armed
        : ControlSessionState::Suspended;
}

ControlInvalidationResult ControlSessionGuard::invalidate(
    ControlInvalidationCause cause
) noexcept
{
    Q_UNUSED(cause)
    if (state_ != ControlSessionState::Armed &&
        state_ != ControlSessionState::Moving) {
        return {};
    }
    state_ = ControlSessionState::Suspended;
    return {true, true};
}

bool ControlSessionGuard::conditionsRestored(const ControlContext &context)
{
    if (state_ != ControlSessionState::Suspended ||
        !movementAvailability(context).allowed) {
        return false;
    }
    state_ = ControlSessionState::Locked;
    return true;
}

void ControlSessionGuard::forceLocked() noexcept
{
    state_ = ControlSessionState::Locked;
}
