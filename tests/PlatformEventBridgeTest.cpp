#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

#include "app/DeviceControlController.h"
#include "app/PlatformEventBridge.h"
#include "event_center/EventCenterService.h"

namespace {

int countType(const QList<SecurityEventRecord> &events, SecurityEventType type)
{
    return static_cast<int>(std::count_if(
        events.cbegin(), events.cend(),
        [type](const auto &event) { return event.eventType == type; }));
}

ControlAttemptSnapshot attempt(DeviceCommand command,
                               ControlAttemptOutcome outcome,
                               ControlDecisionReason reason =
                                   ControlDecisionReason::None)
{
    ControlAttemptSnapshot result;
    result.attemptId = QStringLiteral("attempt-%1-%2")
        .arg(static_cast<int>(command)).arg(static_cast<int>(outcome));
    result.observedAtUtc = QDateTime::currentDateTimeUtc();
    result.command = command;
    result.targetStreamId = 7;
    result.targetDeviceId = QStringLiteral("vehicle-01");
    result.localOutcome = outcome;
    result.reason = reason;
    result.source = ControlAttemptSource::Button;
    return result;
}

} // namespace

class PlatformEventBridgeTest final : public QObject
{
    Q_OBJECT

private slots:
    void mapsEdgesWithoutInitialOrRemovalNoise();
    void mapsControlFailuresAndShutdownHonestly();
    void resourceIdsAreStableAndSanitized();
};

void PlatformEventBridgeTest::mapsEdgesWithoutInitialOrRemovalNoise()
{
    QTemporaryDir directory;
    EventCenterService service(directory.filePath(QStringLiteral("events.json")));
    QVERIFY(service.initialize());
    PlatformEventBridge bridge(&service);
    MediaServerEndpoint endpoint;
    bridge.setMediaServerEndpoint(endpoint);

    bridge.observeMqttState(MqttConnectionState::Disabled);
    bridge.observeMqttState(MqttConnectionState::Disconnected);
    QCOMPARE(countType(service.events(), SecurityEventType::MqttConnectionLost), 0);
    bridge.observeMqttState(MqttConnectionState::Connected);
    bridge.observeMqttState(MqttConnectionState::Disconnected);
    bridge.observeMqttState(MqttConnectionState::Error);
    QCOMPARE(countType(service.events(), SecurityEventType::MqttConnectionLost), 1);
    QCOMPARE(service.events().first().occurrenceCount, quint64 {1});
    bridge.observeMqttState(MqttConnectionState::Connected);
    QCOMPARE(service.events().first().state, SecurityEventState::Resolved);

    bridge.observeDeviceBound(QStringLiteral("vehicle-01"));
    bridge.observePresence(QStringLiteral("vehicle-01"), DevicePresenceState::Offline);
    QCOMPARE(countType(service.events(), SecurityEventType::DevicePresenceLost), 0);
    bridge.observePresence(QStringLiteral("vehicle-01"), DevicePresenceState::Online);
    bridge.observePresence(QStringLiteral("vehicle-01"), DevicePresenceState::Offline);
    QCOMPARE(countType(service.events(), SecurityEventType::DevicePresenceLost), 1);
    bridge.observeDeviceUnbound(QStringLiteral("vehicle-01"));

    StreamEventObservation stream;
    stream.streamId = 7;
    stream.localResourceId = QStringLiteral("camera:front");
    stream.deviceId = QStringLiteral("vehicle-01");
    stream.displayName = QStringLiteral("前视摄像头");
    stream.playbackStatus = DeviceStatus::Disconnected;
    bridge.observeStream(stream);
    QCOMPARE(countType(service.events(), SecurityEventType::VideoStreamLost), 0);
    stream.playbackStatus = DeviceStatus::Playing;
    bridge.observeStream(stream);
    stream.playbackStatus = DeviceStatus::Reconnecting;
    bridge.observeStream(stream);
    QCOMPARE(countType(service.events(), SecurityEventType::VideoStreamLost), 1);
    stream.removing = true;
    bridge.observeStream(stream);
    bridge.observeStreamRemoved(stream);
    QCOMPARE(countType(service.events(), SecurityEventType::VideoStreamLost), 1);

    MediaServerHealth health;
    health.state = MediaServerState::Unavailable;
    bridge.observeMediaHealth(health);
    QCOMPARE(countType(service.events(), SecurityEventType::MediaServerUnhealthy), 0);
    health.state = MediaServerState::Healthy;
    bridge.observeMediaHealth(health);
    health.state = MediaServerState::Degraded;
    bridge.observeMediaHealth(health);
    QCOMPARE(countType(service.events(), SecurityEventType::MediaServerUnhealthy), 1);
}

void PlatformEventBridgeTest::mapsControlFailuresAndShutdownHonestly()
{
    QTemporaryDir directory;
    EventCenterService service(directory.filePath(QStringLiteral("events.json")));
    QVERIFY(service.initialize());
    PlatformEventBridge bridge(&service);

    bridge.observeControlAttempt(attempt(
        DeviceCommand::MoveForward, ControlAttemptOutcome::PublishFailed,
        ControlDecisionReason::MqttDisconnected));
    QCOMPARE(countType(service.events(),
                       SecurityEventType::LocalControlPublishFailed), 1);
    bridge.observeControlAttempt(attempt(
        DeviceCommand::MoveForward, ControlAttemptOutcome::Submitted));
    for (const auto &event : service.events()) {
        if (event.eventType == SecurityEventType::LocalControlPublishFailed)
            QCOMPARE(event.state, SecurityEventState::Resolved);
    }

    bridge.observeControlAttempt(attempt(
        DeviceCommand::StopCar, ControlAttemptOutcome::PublishFailed,
        ControlDecisionReason::MqttDisconnected));
    QCOMPARE(countType(service.events(),
                       SecurityEventType::LocalSafetyStopPublishFailed), 1);
    ControlAttemptSnapshot unavailable = attempt(
        DeviceCommand::StopCar, ControlAttemptOutcome::Rejected,
        ControlDecisionReason::MqttDisconnected);
    unavailable.attemptId = QStringLiteral("unavailable-stop");
    bridge.observeControlAttempt(unavailable);
    QCOMPARE(countType(service.events(),
                       SecurityEventType::LocalSafetyStopUnavailable), 1);

    bridge.observeMqttState(MqttConnectionState::Connected);
    bridge.beginShutdown();
    bridge.observeMqttState(MqttConnectionState::Disconnected);
    QCOMPARE(countType(service.events(), SecurityEventType::MqttConnectionLost), 0);
    ControlAttemptSnapshot exitStop = attempt(
        DeviceCommand::StopCar, ControlAttemptOutcome::PublishFailed,
        ControlDecisionReason::MqttDisconnected);
    exitStop.attemptId = QStringLiteral("exit-stop-failed");
    exitStop.source = ControlAttemptSource::ApplicationExit;
    bridge.observeControlAttempt(exitStop);
    QCOMPARE(countType(service.events(),
                       SecurityEventType::LocalSafetyStopPublishFailed), 1);
    QCOMPARE(service.events().last().linkedControlAttempts.last()
                 .executionConfirmation,
             QStringLiteral("unavailable"));
}

void PlatformEventBridgeTest::resourceIdsAreStableAndSanitized()
{
    QCOMPARE(StreamConnectionController::stableEventResourceId(
                 QStringLiteral("front"), QStringLiteral("vehicle-01"),
                 QStringLiteral("rtmp://user:pass@example.invalid/live/a?token=x")),
             QStringLiteral("camera:front"));
    QCOMPARE(StreamConnectionController::stableEventResourceId(
                 {}, QStringLiteral("vehicle-01"),
                 QStringLiteral("rtmp://example.invalid/live/a")),
             QStringLiteral("device-stream:vehicle-01"));
    const QString hashed = StreamConnectionController::stableEventResourceId(
        {}, {}, QStringLiteral("rtmp://user:pass@EXAMPLE.invalid/live/a?token=x#f"));
    QVERIFY(hashed.startsWith(QStringLiteral("stream-url-sha256:")));
    QVERIFY(!hashed.contains(QStringLiteral("pass")));
    QVERIFY(!hashed.contains(QStringLiteral("token")));
    QCOMPARE(hashed, StreamConnectionController::stableEventResourceId(
        {}, {}, QStringLiteral("rtmp://example.invalid/live/a?token=y")));
}

QTEST_APPLESS_MAIN(PlatformEventBridgeTest)
#include "PlatformEventBridgeTest.moc"
