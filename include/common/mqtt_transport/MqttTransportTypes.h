#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

#include <cstdint>

enum class MqttTransportProtocol { V311, V5 };
enum class MqttTransportOverflowPolicy { DropOldest, FailConnection };
enum class MqttTransportState {
    Disabled, Disconnected, Connecting, Subscribing, Ready, Reconnecting, Error
};

struct MqttTransportConfig final {
    bool enabled = false;
    QString brokerUrl;
    QString clientId;
    int keepAliveSeconds = 30;
    MqttTransportProtocol protocol = MqttTransportProtocol::V311;
    bool automaticReconnect = true;
    qsizetype maximumPayloadBytes = 4096;
    int maximumPendingMessages = 64;
    bool truncateOversizePayload = false;
    MqttTransportOverflowPolicy overflowPolicy =
        MqttTransportOverflowPolicy::DropOldest;
};

struct MqttSubscription final {
    QString topic;
    int qos = 0;
    bool noLocal = false;
    int retainHandling = 0;
};

struct MqttPublishRequest final {
    QString topic;
    QByteArray payload;
    int qos = 0;
    bool retained = false;
    quint32 expirySeconds = 0;
};

struct MqttInboundMessage final {
    QString topic;
    QByteArray payload;
    qsizetype originalPayloadSize = 0;
    int qos = 0;
    bool duplicate = false;
    bool retained = false;
    qint64 expirySeconds = -1;
    qint64 receivedAtMs = 0;
    std::uint64_t connectionEpoch = 0;
};

struct MqttTransportSnapshot final {
    MqttTransportState state = MqttTransportState::Disconnected;
    std::uint64_t generation = 0;
    std::uint64_t connectionEpoch = 0;
    quint64 droppedMessages = 0;
};

Q_DECLARE_METATYPE(MqttTransportState)
Q_DECLARE_METATYPE(MqttInboundMessage)
Q_DECLARE_METATYPE(MqttTransportSnapshot)
