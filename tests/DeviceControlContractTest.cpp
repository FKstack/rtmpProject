#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "device_control/DeviceCommandCodec.h"
#include "device_control/MqttSettingsRepository.h"

class DeviceControlContractTest final : public QObject
{
    Q_OBJECT

private slots:
    void encodesCommand_data();
    void encodesCommand();
    void defaultsAreOfflineAndAcceptBlankBroker();
    void settingsRoundTripAndValidation();
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
}

void DeviceControlContractTest::defaultsAreOfflineAndAcceptBlankBroker()
{
    const MqttConnectionOptions defaults;
    QVERIFY(!defaults.enabled);
    QVERIFY(defaults.brokerUrl.isEmpty());
    QCOMPARE(defaults.topic, QStringLiteral("device/control"));

    QString error;
    QVERIFY2(MqttSettingsRepository::validate(defaults, &error),
             qPrintable(error));

    MqttConnectionOptions enabled = defaults;
    enabled.enabled = true;
    QVERIFY(!MqttSettingsRepository::validate(enabled, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(DeviceControlContractTest)
#include "DeviceControlContractTest.moc"
