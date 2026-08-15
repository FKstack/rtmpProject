#include "device_control/DeviceCommandCodec.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QByteArray DeviceCommandCodec::encode(DeviceCommand command, qint64 timestampMs,
                                      const QString &streamUrl)
{
    QString action;
    QJsonObject data;
    switch (command) {
    case DeviceCommand::StartStream:
        action = QStringLiteral("startStream");
        if (!streamUrl.isEmpty()) {
            data.insert(QStringLiteral("url"), streamUrl);
        }
        break;
    case DeviceCommand::StopStream:
        action = QStringLiteral("stopStream");
        break;
    case DeviceCommand::MoveForward:
        action = QStringLiteral("moveCar");
        data.insert(QStringLiteral("direction"), QStringLiteral("up"));
        break;
    case DeviceCommand::MoveBackward:
        action = QStringLiteral("moveCar");
        data.insert(QStringLiteral("direction"), QStringLiteral("down"));
        break;
    case DeviceCommand::TurnLeft:
        action = QStringLiteral("moveCar");
        data.insert(QStringLiteral("direction"), QStringLiteral("left"));
        break;
    case DeviceCommand::TurnRight:
        action = QStringLiteral("moveCar");
        data.insert(QStringLiteral("direction"), QStringLiteral("right"));
        break;
    case DeviceCommand::StopCar:
        action = QStringLiteral("stopCar");
        break;
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("action"), action},
        {QStringLiteral("data"), data},
        {QStringLiteral("timestamp"), timestampMs}
    }).toJson(QJsonDocument::Compact);
}

QByteArray DeviceCommandCodec::redactForDisplay(const QByteArray &payload)
{
    QString safeText = QString::fromUtf8(payload);
    static const QRegularExpression endpointPattern(
        QStringLiteral(R"(\brtmps?://[^\s"\\}\]]+)"),
        QRegularExpression::CaseInsensitiveOption);
    safeText.replace(endpointPattern, QStringLiteral("<stream-url>"));
    const QByteArray endpointRedacted = safeText.toUtf8();
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(endpointRedacted, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return endpointRedacted;
    }
    QJsonObject root = document.object();
    QJsonObject data = root.value(QStringLiteral("data")).toObject();
    if (!data.contains(QStringLiteral("url"))) return endpointRedacted;
    data.insert(QStringLiteral("url"), QStringLiteral("<stream-url>"));
    root.insert(QStringLiteral("data"), data);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
