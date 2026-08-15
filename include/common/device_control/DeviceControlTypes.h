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

enum class DevicePresenceState
{
    Unavailable = 0,
    Waiting = 1,
    Online = 2,
    Offline = 3
};

struct DeviceHeartbeat
{
    QString clientId;
    qint64 deviceTimestamp = 0;
    qint64 receivedAtMonotonicMs = 0;
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
    QString statusTopic = QStringLiteral("device/status");
    int keepAliveSeconds = 30;
};

Q_DECLARE_METATYPE(DeviceCommand)
Q_DECLARE_METATYPE(MqttConnectionState)
Q_DECLARE_METATYPE(DevicePresenceState)
Q_DECLARE_METATYPE(DeviceHeartbeat)
Q_DECLARE_METATYPE(MqttObservedMessage)
