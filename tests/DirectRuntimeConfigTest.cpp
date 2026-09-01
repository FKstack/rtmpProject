#include "mqtt_signaling/DirectRuntimeConfig.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace rtmp::p2p;

class DirectRuntimeConfigTest final : public QObject
{
    Q_OBJECT
private slots:
    void acceptsExplicitAnonymousConfig();
    void rejectsWrongRoleAndUnknownFields();
};

namespace {
QString writeConfig(QTemporaryDir &directory, const QByteArray &contents,
                    const QString &name)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size())
        return {};
    file.close();
    return path;
}

QByteArray operatorConfig()
{
    return R"({"schemaVersion":1,"enabled":true,"transport":"tcp","authMode":"anonymous","hostname":"broker.invalid","port":1883,"role":"operator","clientId":"operator-test-desktop-signal","deviceId":"device-1","operatorId":"operator-1","clientInstanceId":"desktop-1"})";
}
}

void DirectRuntimeConfigTest::acceptsExplicitAnonymousConfig()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeConfig(directory, operatorConfig(),
                                     QStringLiteral("operator.json"));
    const auto result = loadDirectRuntimeConfig(path, DirectRuntimeRole::Operator);
    QVERIFY(result.ok);
    QVERIFY(result.config.enabled);
    QCOMPARE(result.config.brokerUrl, QStringLiteral("mqtt://broker.invalid:1883"));
    QCOMPARE(result.config.identity.deviceId, std::string("device-1"));
}

void DirectRuntimeConfigTest::rejectsWrongRoleAndUnknownFields()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeConfig(directory, operatorConfig(),
                                     QStringLiteral("operator.json"));
    QCOMPARE(loadDirectRuntimeConfig(path, DirectRuntimeRole::Device).error,
             QStringLiteral("config_value_invalid"));
    QByteArray unknown = operatorConfig();
    unknown.chop(1);
    unknown.append(",\"endpointDefault\":\"forbidden\"}");
    const QString unknownPath = writeConfig(directory, unknown,
                                            QStringLiteral("unknown.json"));
    QCOMPARE(loadDirectRuntimeConfig(unknownPath, DirectRuntimeRole::Operator).error,
             QStringLiteral("config_schema_invalid"));
}

QTEST_APPLESS_MAIN(DirectRuntimeConfigTest)
#include "DirectRuntimeConfigTest.moc"
