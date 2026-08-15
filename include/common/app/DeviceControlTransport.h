#pragma once

#include <QObject>

#include "device_control/DeviceControlTypes.h"

/** Application-owned port around the unstable MQTT transport boundary. */
class DeviceControlTransport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~DeviceControlTransport() override = default;

    [[nodiscard]] virtual MqttConnectionState state() const noexcept = 0;
    virtual void connectToBroker(const MqttConnectionOptions &options) = 0;
    virtual void disconnectFromBroker() = 0;
    virtual bool publish(DeviceCommand command) = 0;
    virtual bool publishStartStream(const QString &streamUrl) = 0;

signals:
    void stateChanged(MqttConnectionState state, const QString &detail);
    void commandSubmitted(DeviceCommand command);
    void commandFailed(DeviceCommand command, const QString &detail);
    void messageReceived(const MqttObservedMessage &message);
    void observedMessagesDropped(quint64 count);
};

class MqttDeviceClient;

/** Signal-preserving adapter; the Paho-owning client remains unchanged. */
class MqttDeviceControlTransportAdapter final : public DeviceControlTransport
{
    Q_OBJECT

public:
    explicit MqttDeviceControlTransportAdapter(
        MqttDeviceClient *client,
        QObject *parent = nullptr
    );

    [[nodiscard]] MqttConnectionState state() const noexcept override;
    void connectToBroker(const MqttConnectionOptions &options) override;
    void disconnectFromBroker() override;
    bool publish(DeviceCommand command) override;
    bool publishStartStream(const QString &streamUrl) override;

private:
    MqttDeviceClient *client_ = nullptr;
};
