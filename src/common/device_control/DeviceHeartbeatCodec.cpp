#include "device_control/DeviceHeartbeatCodec.h"

#include <QJsonDocument>
#include <QJsonObject>
#include "device_control/DeviceIdentity.h"

namespace {

void assignError(QString *target, const QString &message)
{
    if (target != nullptr) *target = message;
}

} // namespace

std::optional<DeviceHeartbeat> DeviceHeartbeatCodec::decode(
    const QByteArray &payload, qint64 receivedAtMonotonicMs, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        assignError(error, QStringLiteral("设备状态消息不是有效 JSON 对象。"));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("type")).toString() !=
        QStringLiteral("heartbeat")) {
        assignError(error, QStringLiteral("设备状态消息 type 不是 heartbeat。"));
        return std::nullopt;
    }

    const QString clientId =
        root.value(QStringLiteral("client_id")).toString().trimmed();
    if (clientId.isEmpty() ||
        clientId.size() > kMaximumClientIdLength ||
        !DeviceIdentity::isValid(clientId)) {
        assignError(error, QStringLiteral("设备状态消息 client_id 无效。"));
        return std::nullopt;
    }

    const QJsonValue timestampValue = root.value(QStringLiteral("timestamp"));
    const qint64 timestamp = timestampValue.toInteger(-1);
    if (!timestampValue.isDouble() || timestamp < 0) {
        assignError(error, QStringLiteral("设备状态消息 timestamp 无效。"));
        return std::nullopt;
    }

    assignError(error, {});
    return DeviceHeartbeat{clientId, timestamp, receivedAtMonotonicMs};
}
