#include "mqtt_signaling/DirectRuntimeConfig.h"

#include "identity_contracts/IdentityContracts.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace rtmp::p2p {
namespace {
bool exactFields(const QJsonObject &object)
{
    static const QSet<QString> required{
        QStringLiteral("schemaVersion"), QStringLiteral("enabled"),
        QStringLiteral("transport"), QStringLiteral("authMode"),
        QStringLiteral("hostname"), QStringLiteral("port"),
        QStringLiteral("role"), QStringLiteral("clientId"),
        QStringLiteral("deviceId"), QStringLiteral("operatorId"),
        QStringLiteral("clientInstanceId")};
    const QStringList keys = object.keys();
    const QSet<QString> actual(keys.begin(), keys.end());
    return actual == required;
}
}

DirectRuntimeConfigResult loadDirectRuntimeConfig(
    const QString &path, DirectRuntimeRole expectedRole)
{
    if (path.trimmed().isEmpty()) return {false, QStringLiteral("config_path_empty"), {}};
    QFile file(QFileInfo(path).absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {false, QStringLiteral("config_open_failed"), {}};
    if (file.size() < 2 || file.size() > 64 * 1024)
        return {false, QStringLiteral("config_size_invalid"), {}};
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !exactFields(document.object()))
        return {false, QStringLiteral("config_schema_invalid"), {}};
    const QJsonObject object = document.object();
    const QString role = object.value(QStringLiteral("role")).toString();
    const QString expected = expectedRole == DirectRuntimeRole::Operator
        ? QStringLiteral("operator") : QStringLiteral("device");
    const QString hostname = object.value(QStringLiteral("hostname")).toString().trimmed();
    const QString clientId = object.value(QStringLiteral("clientId")).toString().trimmed();
    const QString device = object.value(QStringLiteral("deviceId")).toString().trimmed();
    const QString operatorId = object.value(QStringLiteral("operatorId")).toString().trimmed();
    const QString instance = object.value(QStringLiteral("clientInstanceId")).toString().trimmed();
    const int port = object.value(QStringLiteral("port")).toInt(-1);
    const bool enabled = object.value(QStringLiteral("enabled")).toBool(false);
    if (object.value(QStringLiteral("schemaVersion")).toInt() != 1
        || !enabled || object.value(QStringLiteral("transport")).toString() != QStringLiteral("tcp")
        || object.value(QStringLiteral("authMode")).toString() != QStringLiteral("anonymous")
        || role != expected || hostname.isEmpty() || hostname.contains(QLatin1Char('/'))
        || hostname.contains(QLatin1Char('@')) || port < 1 || port > 65535
        || clientId.isEmpty() || !DeviceId::parse(device.toStdString())
        || !UserId::parse(operatorId.toStdString())
        || !ClientInstanceId::parse(instance.toStdString()))
        return {false, QStringLiteral("config_value_invalid"), {}};
    DirectRuntimeConfig config;
    config.enabled = true;
    config.brokerUrl = QStringLiteral("mqtt://%1:%2").arg(hostname).arg(port);
    config.clientId = clientId;
    config.role = expectedRole;
    config.identity = {device.toStdString(), operatorId.toStdString(),
                       instance.toStdString()};
    return {true, {}, std::move(config)};
}

} // namespace rtmp::p2p
