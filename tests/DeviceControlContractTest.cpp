#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>

#include "device_control/DeviceCommandCodec.h"
#include "device_control/DeviceHeartbeatCodec.h"
#include "device_control/DevicePresenceTracker.h"
#include "device_control/MqttSettingsRepository.h"

class DeviceControlContractTest final : public QObject
{
    Q_OBJECT

private slots:
    void encodesCommand_data();
    void encodesCommand();
    void defaultsAreOfflineAndAcceptBlankBroker();
    void settingsRoundTripAndValidation();
    void migratesSchemaV1Settings();
    void parsesHeartbeatContract();
    void tracksHeartbeatTimeoutAndBoundsCache();
};

void DeviceControlContractTest::encodesCommand_data()
{
    QTest::addColumn<DeviceCommand>("command");
    QTest::addColumn<QString>("action");
    QTest::addColumn<QString>("direction");
    QTest::newRow("start") << DeviceCommand::StartStream << "startStream" << "";
    QTest::newRow("stop-stream") << DeviceCommand::StopStream << "stopStream" << "";
    QTest::newRow("up") << DeviceCommand::MoveForward << "moveCar" << "up";
    QTest::newRow("down") << DeviceCommand::MoveBackward << "moveCar" << "down";
    QTest::newRow("left") << DeviceCommand::TurnLeft << "moveCar" << "left";
    QTest::newRow("right") << DeviceCommand::TurnRight << "moveCar" << "right";
    QTest::newRow("stop-car") << DeviceCommand::StopCar << "stopCar" << "";
}

void DeviceControlContractTest::encodesCommand()
{
    QFETCH(DeviceCommand, command);
    QFETCH(QString, action);
    QFETCH(QString, direction);
    const qint64 timestamp = 1780413730000LL;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        DeviceCommandCodec::encode(command, timestamp), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    const QJsonObject root = document.object();
    QCOMPARE(root.value("action").toString(), action);
    QCOMPARE(root.value("timestamp").toInteger(), timestamp);
    QVERIFY(root.value("data").isObject());
    QCOMPARE(root.value("data").toObject().value("direction").toString(),
             direction);

    if (command == DeviceCommand::StartStream) {
        const QByteArray startPayload = DeviceCommandCodec::encode(
            command, timestamp,
            QStringLiteral("rtmp://127.0.0.1:1935/live/040001"));
        const QJsonObject start = QJsonDocument::fromJson(startPayload).object();
        QCOMPARE(start.value("data").toObject().value("url").toString(),
                 QStringLiteral("rtmp://127.0.0.1:1935/live/040001"));
        const QByteArray redacted =
            DeviceCommandCodec::redactForDisplay(startPayload);
        QVERIFY(!redacted.contains("127.0.0.1"));
        QVERIFY(redacted.contains("<stream-url>"));
        const QByteArray malformedRedacted =
            DeviceCommandCodec::redactForDisplay(
                "not-json rtmp://127.0.0.1:1935/live/040001");
        QVERIFY(!malformedRedacted.contains("127.0.0.1"));
    }
}

void DeviceControlContractTest::settingsRoundTripAndValidation()
{
    QTemporaryDir directory;
    MqttSettingsRepository repository(directory.filePath("mqtt.json"));
    MqttConnectionOptions options;
    QString error;
    QVERIFY2(repository.save(options, &error), qPrintable(error));
    const MqttSettingsLoadResult loaded = repository.load();
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.options.brokerUrl, options.brokerUrl);
    QCOMPARE(loaded.options.topic, options.topic);
    QCOMPARE(loaded.options.statusTopic, options.statusTopic);

    options.enabled = true;
    options.brokerUrl = QStringLiteral("mqtt://127.0.0.1:1883");
    QVERIFY2(repository.save(options, &error), qPrintable(error));
    const MqttSettingsLoadResult enabledLoaded = repository.load();
    QVERIFY2(enabledLoaded.ok(), qPrintable(enabledLoaded.error));
    QVERIFY(enabledLoaded.options.enabled);
    QCOMPARE(enabledLoaded.options.brokerUrl, options.brokerUrl);

    options.brokerUrl = QStringLiteral("mqtt://user:secret@host:1883");
    QVERIFY(!MqttSettingsRepository::validate(options, &error));
    options.brokerUrl = QStringLiteral("mqtt://host:1883/device");
    QVERIFY(!MqttSettingsRepository::validate(options, &error));
    options.brokerUrl = QStringLiteral("mqtt://host:1883");
    options.topic = QStringLiteral("device/+");
    QVERIFY(!MqttSettingsRepository::validate(options, &error));
    options.topic = QStringLiteral("device/control");
    options.statusTopic = QStringLiteral("device/#");
    QVERIFY(!MqttSettingsRepository::validate(options, &error));
}

void DeviceControlContractTest::defaultsAreOfflineAndAcceptBlankBroker()
{
    const MqttConnectionOptions defaults;
    QVERIFY(!defaults.enabled);
    QVERIFY(defaults.brokerUrl.isEmpty());
    QCOMPARE(defaults.topic, QStringLiteral("device/control"));
    QCOMPARE(defaults.statusTopic, QStringLiteral("device/status"));

    QString error;
    QVERIFY2(MqttSettingsRepository::validate(defaults, &error),
             qPrintable(error));

    MqttConnectionOptions enabled = defaults;
    enabled.enabled = true;
    QVERIFY(!MqttSettingsRepository::validate(enabled, &error));
    QVERIFY(!error.isEmpty());
}

void DeviceControlContractTest::migratesSchemaV1Settings()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("mqtt.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"schemaVersion":1,"enabled":false,"brokerUrl":"","topic":"device/control"})");
    file.close();

    MqttSettingsRepository repository(path);
    const MqttSettingsLoadResult loaded = repository.load();
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.options.statusTopic, QStringLiteral("device/status"));
}

void DeviceControlContractTest::parsesHeartbeatContract()
{
    QString error;
    const auto heartbeat = DeviceHeartbeatCodec::decode(
        R"({"type":"heartbeat","client_id":"040001","timestamp":5824750})",
        1234, &error);
    QVERIFY2(heartbeat.has_value(), qPrintable(error));
    QCOMPARE(heartbeat->clientId, QStringLiteral("040001"));
    QCOMPARE(heartbeat->deviceTimestamp, 5824750);
    QCOMPARE(heartbeat->receivedAtMonotonicMs, 1234);

    QVERIFY(!DeviceHeartbeatCodec::decode(
        R"({"type":"status","client_id":"040001","timestamp":1})", 0,
        &error).has_value());
    QVERIFY(!DeviceHeartbeatCodec::decode(
        R"({"type":"heartbeat","client_id":"bad/id","timestamp":1})", 0,
        &error).has_value());
    QVERIFY(!DeviceHeartbeatCodec::decode(
        R"({"type":"heartbeat","client_id":"040001","timestamp":-1})", 0,
        &error).has_value());
}

void DeviceControlContractTest::tracksHeartbeatTimeoutAndBoundsCache()
{
    qint64 now = 10'000;
    DevicePresenceTracker tracker(nullptr, [&now] { return now; });
    tracker.registerDevice(QStringLiteral("040001"));
    QCOMPARE(tracker.state(QStringLiteral("040001")),
             DevicePresenceState::Unavailable);

    tracker.setAvailable(true);
    QCOMPARE(tracker.state(QStringLiteral("040001")),
             DevicePresenceState::Waiting);
    now += DevicePresenceTracker::kOfflineTimeoutMs - 1;
    tracker.refresh();
    QCOMPARE(tracker.state(QStringLiteral("040001")),
             DevicePresenceState::Waiting);

    tracker.processHeartbeat({QStringLiteral("040001"), 5000, now});
    QCOMPARE(tracker.state(QStringLiteral("040001")),
             DevicePresenceState::Online);
    now += DevicePresenceTracker::kOfflineTimeoutMs;
    tracker.refresh();
    QCOMPARE(tracker.state(QStringLiteral("040001")),
             DevicePresenceState::Offline);
    tracker.processHeartbeat({QStringLiteral("040001"), 1, now});
    QCOMPARE(tracker.state(QStringLiteral("040001")),
             DevicePresenceState::Online);

    for (int index = 0; index < 80; ++index) {
        tracker.processHeartbeat({QStringLiteral("unknown-%1").arg(index),
                                  index, now});
    }
    QVERIFY(tracker.trackedDeviceCount() <=
            DevicePresenceTracker::kMaximumTrackedDevices);
    QCOMPARE(tracker.state(QStringLiteral("040001")),
             DevicePresenceState::Online);

    now += DevicePresenceTracker::kOfflineTimeoutMs;
    tracker.refresh();
    QCOMPARE(tracker.trackedDeviceCount(), 1);
}

QTEST_MAIN(DeviceControlContractTest)
#include "DeviceControlContractTest.moc"
