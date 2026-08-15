#include <QTest>

#include "control_policy/ControlSessionGuard.h"

namespace {

ControlContext readyContext()
{
    return {true, true, true, true, 1'000};
}

} // namespace

class ControlSessionGuardTest final : public QObject
{
    Q_OBJECT

private slots:
    void commandMatrix();
    void frameFreshnessBoundary();
    void invalidationRequiresExplicitRearm();
    void stopFailureSuspendsMovingSession();
};

void ControlSessionGuardTest::commandMatrix()
{
    ControlSessionGuard guard;
    ControlContext context = readyContext();

    QVERIFY(guard.decide(ControlIntentKind::StartStream, context).allowed);
    QVERIFY(guard.decide(ControlIntentKind::StopStream, context).allowed);
    QVERIFY(guard.decide(ControlIntentKind::StopCar, context).allowed);
    QCOMPARE(
        guard.decide(ControlIntentKind::Move, context).reason,
        ControlDecisionReason::ControlLocked
    );

    QVERIFY(guard.requestArm(context).allowed);
    QVERIFY(guard.decide(ControlIntentKind::Move, context).allowed);
    guard.applyOutcome(
        ControlIntentKind::Move,
        ControlAttemptOutcome::Submitted
    );
    QCOMPARE(guard.state(), ControlSessionState::Moving);

    context.heartbeatOnline = false;
    QVERIFY(guard.decide(ControlIntentKind::StopStream, context).allowed);
    QVERIFY(guard.decide(ControlIntentKind::StopCar, context).allowed);
    QCOMPARE(
        guard.decide(ControlIntentKind::StartStream, context).reason,
        ControlDecisionReason::DeviceNotOnline
    );
}

void ControlSessionGuardTest::frameFreshnessBoundary()
{
    ControlSessionGuard guard;
    ControlContext context = readyContext();
    QVERIFY(guard.requestArm(context).allowed);
    QVERIFY(guard.decide(ControlIntentKind::Move, context).allowed);

    context.presentedFrameAgeMs = 1'001;
    QCOMPARE(
        guard.decide(ControlIntentKind::Move, context).reason,
        ControlDecisionReason::FrameStale
    );
    context.presentedFrameAgeMs = -1;
    QCOMPARE(
        guard.decide(ControlIntentKind::Move, context).reason,
        ControlDecisionReason::FrameUnavailable
    );
}

void ControlSessionGuardTest::invalidationRequiresExplicitRearm()
{
    ControlSessionGuard guard;
    const ControlContext context = readyContext();
    QVERIFY(guard.requestArm(context).allowed);

    const ControlInvalidationResult first = guard.invalidate(
        ControlInvalidationCause::FocusLost
    );
    QVERIFY(first.stateChanged);
    QVERIFY(first.shouldAttemptStop);
    QCOMPARE(guard.state(), ControlSessionState::Suspended);

    const ControlInvalidationResult repeated = guard.invalidate(
        ControlInvalidationCause::FullscreenTransition
    );
    QVERIFY(!repeated.stateChanged);
    QVERIFY(!repeated.shouldAttemptStop);
    QVERIFY(guard.conditionsRestored(context));
    QCOMPARE(guard.state(), ControlSessionState::Locked);
    QVERIFY(guard.requestArm(context).allowed);
}

void ControlSessionGuardTest::stopFailureSuspendsMovingSession()
{
    ControlSessionGuard guard;
    const ControlContext context = readyContext();
    QVERIFY(guard.requestArm(context).allowed);
    guard.applyOutcome(ControlIntentKind::Move, ControlAttemptOutcome::Submitted);
    guard.applyOutcome(
        ControlIntentKind::StopCar,
        ControlAttemptOutcome::PublishFailed
    );
    QCOMPARE(guard.state(), ControlSessionState::Suspended);
}

QTEST_GUILESS_MAIN(ControlSessionGuardTest)

#include "ControlSessionGuardTest.moc"
