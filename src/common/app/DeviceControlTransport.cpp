#include "app/DeviceControlTransport.h"

#include "device_control/MqttDeviceClient.h"

MqttDeviceControlTransportAdapter::MqttDeviceControlTransportAdapter(
    MqttDeviceClient *client,
    QObject *parent
)
    : DeviceControlTransport(parent), client_(client)
{
    Q_ASSERT(client_ != nullptr);
    connect(client_, &MqttDeviceClient::stateChanged,
            this, &DeviceControlTransport::stateChanged);
    connect(client_, &MqttDeviceClient::commandSubmitted,
            this, &DeviceControlTransport::commandSubmitted);
    connect(client_, &MqttDeviceClient::commandFailed,
            this, &DeviceControlTransport::commandFailed);
    connect(client_, &MqttDeviceClient::messageReceived,
            this, &DeviceControlTransport::messageReceived);
    connect(client_, &MqttDeviceClient::observedMessagesDropped,
            this, &DeviceControlTransport::observedMessagesDropped);
}

MqttConnectionState MqttDeviceControlTransportAdapter::state() const noexcept
{
    return client_->state();
}

void MqttDeviceControlTransportAdapter::connectToBroker(
    const MqttConnectionOptions &options
)
{
    client_->connectToBroker(options);
}

void MqttDeviceControlTransportAdapter::disconnectFromBroker()
{
    client_->disconnectFromBroker();
}

bool MqttDeviceControlTransportAdapter::publish(DeviceCommand command)
{
    return client_->publish(command);
}

bool MqttDeviceControlTransportAdapter::publishStartStream(
    const QString &streamUrl
)
{
    return client_->publishStartStream(streamUrl);
}
