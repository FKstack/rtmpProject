#include "device_control/DeviceCommandCodec.h"

#include <QJsonDocument>
#include <QJsonObject>

QByteArray DeviceCommandCodec::encode(DeviceCommand command, qint64 timestampMs)
{
    QString action;
    QJsonObject data;
    switch (command) {
    case DeviceCommand::StartStream:
        action = QStringLiteral("startStream");
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
