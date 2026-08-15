#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <utility>

#include "app/DeviceControlController.h"
#include "app/DeviceControlTransport.h"
#include "device_control/DevicePresenceTracker.h"
#include "logging/LogManager.h"
#include "ui/DeviceControlPanel.h"
#include "ui/MainWindow.h"

namespace {

class FakeDeviceControlTransport final : public DeviceControlTransport
{
public:
    using DeviceControlTransport::DeviceControlTransport;

    MqttConnectionState state() const noexcept override { return state_; }

    void connectToBroker(const MqttConnectionOptions &) override {}

    void disconnectFromBroker() override
    {
        ++disconnectCount;
        transitionTo(MqttConnectionState::Disconnected);
    }

    bool publish(DeviceCommand command) override
    {
        attemptedCommands.append(command);
        const bool accepted = state_ == MqttConnectionState::Connected &&
                              !std::exchange(failNextPublish, false);
        if (accepted) submittedCommands.append(command);
        return accepted;
    }

    bool publishStartStream(const QString &streamUrl) override
    {
        attemptedStartUrls.append(streamUrl);
        const bool accepted = state_ == MqttConnectionState::Connected &&
                              !std::exchange(failNextPublish, false);
        if (accepted) submittedCommands.append(DeviceCommand::StartStream);
        return accepted;
    }

    void transitionTo(MqttConnectionState state)
    {
        state_ = state;
        emit stateChanged(state, QStringLiteral("fake-state"));
    }

    MqttConnectionState state_ = MqttConnectionState::Disconnected;
    QList<DeviceCommand> attemptedCommands;
    QList<DeviceCommand> submittedCommands;
    QStringList attemptedStartUrls;
    bool failNextPublish = false;
    int disconnectCount = 0;
};

struct ControllerFixture
{
    ControllerFixture()
        : settingsRepository(directory.filePath(QStringLiteral("mqtt.ini")))
    {
        LoggingOptions options;
        options.directoryPath = directory.path();
        options.minimumLevel = LogLevel::Trace;
        options.consoleEnabled = false;
        logReady = logManager.initialize(options);
        controller = std::make_unique<DeviceControlController>(
            &window,
            &panel,
            &transport,
            &presenceTracker,
            &logManager,
            [this](StreamId) { return media; },
            settingsRepository
        );
    }

    ~ControllerFixture()
    {
        controller.reset();
        logManager.shutdown();
    }

    void selectReadyTarget(
        StreamId streamId = 41,
        const QString &deviceId = QStringLiteral("vehicle-01"),
        const QString &url = QStringLiteral(
            "rtmp://192.0.2.1/live/vehicle-01?token=private"
        )
    )
    {
        transport.transitionTo(MqttConnectionState::Connected);
        controller->setControlTarget(streamId, deviceId, url);
        controller->setDevicePresence(deviceId, DevicePresenceState::Online);
        controller->refreshControlAvailability();
    }

    QByteArray finishAndReadAudit()
    {
        controller.reset();
        const QString auditPath = logManager.auditLogFilePath();
        logManager.shutdown();
        QFile file(auditPath);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }

    QTemporaryDir directory;
    LogManager logManager;
    MainWindow window;
    DeviceControlPanel panel;
    FakeDeviceControlTransport transport;
    DevicePresenceTracker presenceTracker;
    ControlMediaObservation media {true, 0};
    MqttSettingsRepository settingsRepository;
    std::unique_ptr<DeviceControlController> controller;
    bool logReady = false;
};

ControlAttemptSnapshot attemptAt(const QSignalSpy &spy, int index)
{
    return spy.at(index).constFirst().value<ControlAttemptSnapshot>();
}

} // namespace

class DeviceControlControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void sharesGuardAndStopsOldTargetOnce();
    void availabilityLossSuspendsAndRequiresExplicitRearm();
    void retriesDisconnectedSafetyStopAgainstOriginalTarget();
    void recordsTruthfulOutcomesWithoutSensitiveTransportData();
};

void DeviceControlControllerTest::sharesGuardAndStopsOldTargetOnce()
{
    ControllerFixture fixture;
    QVERIFY(fixture.directory.isValid());
    QVERIFY(fixture.logReady);
    fixture.selectReadyTarget();
    QSignalSpy attempts(
        fixture.controller.get(),
        &DeviceControlController::controlAttemptRecorded
    );

    fixture.controller->setControlArmed(true);
    fixture.controller->submitCommand(
        DeviceCommand::MoveForward,
        ControlAttemptSource::Joystick
    );
    QCOMPARE(fixture.transport.submittedCommands,
             QList<DeviceCommand> {DeviceCommand::MoveForward});

    fixture.controller->invalidateControl(
        ControlInvalidationCause::FocusLost,
        ControlAttemptSource::FocusLost
    );
    QCOMPARE(fixture.transport.submittedCommands.count(DeviceCommand::StopCar),
             1);
    fixture.controller->invalidateControl(
        ControlInvalidationCause::FullscreenTransition,
        ControlAttemptSource::FullscreenTransition
    );
    QCOMPARE(fixture.transport.submittedCommands.count(DeviceCommand::StopCar),
             1);

    fixture.controller->refreshControlAvailability();
    fixture.controller->submitCommand(
        DeviceCommand::TurnLeft,
        ControlAttemptSource::Keyboard
    );
    QCOMPARE(attemptAt(attempts, attempts.count() - 1).localOutcome,
             ControlAttemptOutcome::Rejected);
    QCOMPARE(fixture.transport.submittedCommands.count(DeviceCommand::TurnLeft),
             0);

    fixture.controller->setControlArmed(true);
    fixture.controller->submitCommand(
        DeviceCommand::MoveBackward,
        ControlAttemptSource::Keyboard
    );
    fixture.controller->setControlTarget(
        42,
        QStringLiteral("vehicle-02"),
        QStringLiteral("rtmp://192.0.2.2/live/vehicle-02")
    );
    QCOMPARE(fixture.transport.submittedCommands.count(DeviceCommand::StopCar),
             2);
    const ControlAttemptSnapshot targetStop =
        attemptAt(attempts, attempts.count() - 1);
    QCOMPARE(targetStop.command, DeviceCommand::StopCar);
    QCOMPARE(targetStop.targetStreamId, StreamId {41});
    QCOMPARE(targetStop.targetDeviceId, QStringLiteral("vehicle-01"));
    QCOMPARE(targetStop.source, ControlAttemptSource::TargetChanged);
}

void DeviceControlControllerTest::
availabilityLossSuspendsAndRequiresExplicitRearm()
{
    for (int scenario = 0; scenario < 3; ++scenario) {
        ControllerFixture fixture;
        QVERIFY(fixture.logReady);
        fixture.selectReadyTarget();
        QSignalSpy attempts(
            fixture.controller.get(),
            &DeviceControlController::controlAttemptRecorded
        );
        fixture.controller->setControlArmed(true);
        fixture.controller->submitCommand(
            DeviceCommand::MoveForward,
            ControlAttemptSource::Joystick
        );

        ControlAttemptSource expectedSource =
            ControlAttemptSource::HeartbeatTimeout;
        if (scenario == 0) {
            fixture.controller->setDevicePresence(
                QStringLiteral("vehicle-01"),
                DevicePresenceState::Offline
            );
        } else if (scenario == 1) {
            fixture.media.playbackPlaying = false;
            expectedSource = ControlAttemptSource::PlaybackInterrupted;
            fixture.controller->refreshControlAvailability();
        } else {
            fixture.media.presentedFrameAgeMs = 1'001;
            expectedSource = ControlAttemptSource::FrameStale;
            fixture.controller->refreshControlAvailability();
        }

        QCOMPARE(fixture.transport.submittedCommands.count(
                     DeviceCommand::StopCar), 1);
        QCOMPARE(attemptAt(attempts, attempts.count() - 1).source,
                 expectedSource);
        fixture.controller->refreshControlAvailability();
        QCOMPARE(fixture.transport.submittedCommands.count(
                     DeviceCommand::StopCar), 1);

        fixture.media = {true, 0};
        fixture.controller->setDevicePresence(
            QStringLiteral("vehicle-01"), DevicePresenceState::Online);
        fixture.controller->refreshControlAvailability();
        fixture.controller->submitCommand(
            DeviceCommand::TurnLeft,
            ControlAttemptSource::Keyboard
        );
        QCOMPARE(attemptAt(attempts, attempts.count() - 1).localOutcome,
                 ControlAttemptOutcome::Rejected);
        QCOMPARE(attemptAt(attempts, attempts.count() - 1).reason,
                 ControlDecisionReason::ControlLocked);
    }
}

void DeviceControlControllerTest::
retriesDisconnectedSafetyStopAgainstOriginalTarget()
{
    ControllerFixture fixture;
    QVERIFY(fixture.logReady);
    fixture.selectReadyTarget();
    QSignalSpy attempts(
        fixture.controller.get(),
        &DeviceControlController::controlAttemptRecorded
    );

    fixture.controller->setControlArmed(true);
    fixture.controller->submitCommand(
        DeviceCommand::TurnRight,
        ControlAttemptSource::Button
    );
    fixture.transport.transitionTo(MqttConnectionState::Disconnected);

    const ControlAttemptSnapshot rejectedStop =
        attemptAt(attempts, attempts.count() - 1);
    QCOMPARE(rejectedStop.command, DeviceCommand::StopCar);
    QCOMPARE(rejectedStop.localOutcome, ControlAttemptOutcome::Rejected);
    QCOMPARE(rejectedStop.reason, ControlDecisionReason::MqttDisconnected);
    QCOMPARE(rejectedStop.source, ControlAttemptSource::MqttDisconnected);
    QCOMPARE(fixture.transport.attemptedCommands.count(DeviceCommand::StopCar),
             0);

    fixture.transport.transitionTo(MqttConnectionState::Connected);
    const ControlAttemptSnapshot retriedStop =
        attemptAt(attempts, attempts.count() - 1);
    QCOMPARE(retriedStop.command, DeviceCommand::StopCar);
    QCOMPARE(retriedStop.localOutcome, ControlAttemptOutcome::Submitted);
    QCOMPARE(retriedStop.targetStreamId, rejectedStop.targetStreamId);
    QCOMPARE(retriedStop.targetDeviceId, rejectedStop.targetDeviceId);
    QCOMPARE(retriedStop.source, rejectedStop.source);
    QCOMPARE(fixture.transport.submittedCommands.count(DeviceCommand::StopCar),
             1);

    fixture.controller->submitCommand(
        DeviceCommand::MoveForward,
        ControlAttemptSource::Joystick
    );
    QCOMPARE(attemptAt(attempts, attempts.count() - 1).reason,
             ControlDecisionReason::ControlLocked);

    fixture.controller->setControlArmed(true);
    fixture.controller->submitCommand(
        DeviceCommand::MoveBackward,
        ControlAttemptSource::Joystick
    );
    fixture.controller->stop();
    QCOMPARE(attemptAt(attempts, attempts.count() - 1).source,
             ControlAttemptSource::ApplicationExit);
    QCOMPARE(fixture.transport.submittedCommands.count(DeviceCommand::StopCar),
             2);
    fixture.controller->stop();
    QCOMPARE(fixture.transport.disconnectCount, 1);
}

void DeviceControlControllerTest::
recordsTruthfulOutcomesWithoutSensitiveTransportData()
{
    ControllerFixture fixture;
    QVERIFY(fixture.logReady);
    const QString fullUrl = QStringLiteral(
        "rtmp://192.0.2.1/live/vehicle-01?token=private"
    );
    fixture.selectReadyTarget(41, QStringLiteral("vehicle-01"), fullUrl);
    QSignalSpy attempts(
        fixture.controller.get(),
        &DeviceControlController::controlAttemptRecorded
    );

    fixture.controller->submitCommand(
        DeviceCommand::StartStream,
        ControlAttemptSource::Button
    );
    fixture.transport.failNextPublish = true;
    fixture.controller->submitCommand(
        DeviceCommand::StopStream,
        ControlAttemptSource::Button
    );
    fixture.transport.transitionTo(MqttConnectionState::Disconnected);
    fixture.controller->submitCommand(
        DeviceCommand::StopCar,
        ControlAttemptSource::Button
    );

    QCOMPARE(attempts.count(), 3);
    QCOMPARE(attemptAt(attempts, 0).localOutcome,
             ControlAttemptOutcome::Submitted);
    QCOMPARE(attemptAt(attempts, 1).localOutcome,
             ControlAttemptOutcome::PublishFailed);
    QCOMPARE(attemptAt(attempts, 2).localOutcome,
             ControlAttemptOutcome::Rejected);
    for (int index = 0; index < attempts.count(); ++index) {
        QVERIFY(!attemptAt(attempts, index).attemptId.isEmpty());
    }

    const QByteArray audit = fixture.finishAndReadAudit();
    QVERIFY(!audit.isEmpty());
    QVERIFY(audit.contains("\"action\":\"CONTROL_COMMAND_ATTEMPT\""));
    QVERIFY(audit.contains("\"result\":\"SUBMITTED\""));
    QVERIFY(audit.contains("\"result\":\"PUBLISH_FAILED\""));
    QVERIFY(audit.contains("\"result\":\"REJECTED\""));
    QVERIFY(audit.contains("\"executionConfirmation\":\"unavailable\""));
    QVERIFY(audit.contains("\"actorAssurance\":\"unverified-local\""));
    QVERIFY(!audit.contains(fullUrl.toUtf8()));
    QVERIFY(!audit.contains("token=private"));
    QVERIFY(!audit.contains("brokerUrl"));
    QVERIFY(!audit.contains("rawPayload"));
}

QTEST_MAIN(DeviceControlControllerTest)

#include "DeviceControlControllerTest.moc"
