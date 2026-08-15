#pragma once

#include <QByteArray>
#include <QString>
#include <QMetaType>

enum class DeviceCommand
{
    StartStream,
    StopStream,
    MoveForward,
    MoveBackward,
    TurnLeft,
    TurnRight,
    StopCar
};

enum class MqttConnectionState
{
    Disabled = 0,
    Disconnected = 1,
    Connecting = 2,
    Connected = 3,
    Reconnecting = 4,
    Error = 5,
    Subscribing = 6
};

struct MqttObservedMessage
{
    QString topic;
    QByteArray payload;
    qint64 receivedAtMs = 0;
    qsizetype originalPayloadSize = 0;
};

struct MqttConnectionOptions
{
    bool enabled = false;
    QString brokerUrl;
    QString topic = QStringLiteral("device/control");
    int keepAliveSeconds = 30;
};

Q_DECLARE_METATYPE(DeviceCommand)
Q_DECLARE_METATYPE(MqttConnectionState)
Q_DECLARE_METATYPE(MqttObservedMessage)
