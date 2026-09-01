#pragma once

#include <QObject>
#include <memory>

#include "device_control/DeviceControlTypes.h"

class MqttAsyncTransport;

/** Owns one Paho MQTTAsync session; public methods belong to the Qt owner thread. */
class MqttDeviceClient final : public QObject
{
    Q_OBJECT

public:
    static constexpr qsizetype kMaximumObservedPayloadBytes = 4096;
    static constexpr qsizetype kMaximumPendingObservedMessages = 64;
    static constexpr int kSubscribeTimeoutMs = 5000;

    explicit MqttDeviceClient(QObject *parent = nullptr);
    ~MqttDeviceClient() override;

    MqttDeviceClient(const MqttDeviceClient &) = delete;
    MqttDeviceClient &operator=(const MqttDeviceClient &) = delete;

    [[nodiscard]] MqttConnectionState state() const noexcept;
    [[nodiscard]] MqttConnectionOptions options() const;

public slots:
    void connectToBroker(const MqttConnectionOptions &options);
    void disconnectFromBroker();
    bool publish(DeviceCommand command);
    bool publishStartStream(const QString &streamUrl);

signals:
    void stateChanged(MqttConnectionState state, const QString &detail);
    void commandSubmitted(DeviceCommand command);
    void commandFailed(DeviceCommand command, const QString &detail);
    void messageReceived(const MqttObservedMessage &message);
    void observedMessagesDropped(quint64 count);

private:
    void setState(MqttConnectionState state, const QString &detail = {});
    void handleTransportState(int state, const QString &detail);
    bool publishPayload(DeviceCommand command, const QByteArray &payload);

    std::unique_ptr<MqttAsyncTransport> transport_;
    MqttConnectionOptions options_;
    MqttConnectionState state_ = MqttConnectionState::Disconnected;
};
