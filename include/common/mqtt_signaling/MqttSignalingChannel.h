#pragma once

#include "mqtt_signaling/DirectRuntimeConfig.h"
#include "signaling_channel/SignalingChannel.h"

#include <QObject>

#include <memory>

class MqttAsyncTransport;

namespace rtmp::p2p {

/** MQTT5/QoS1 adapter; owns an independent shared-transport instance. */
class MqttSignalingChannel final : public QObject, public ISignalingChannel
{
    Q_OBJECT
public:
    explicit MqttSignalingChannel(DirectRuntimeConfig config,
                                  QObject *parent = nullptr);
    ~MqttSignalingChannel() override;

    void setStateHandler(StateHandler handler) override;
    void setMessageHandler(MessageHandler handler) override;
    bool start(const std::vector<std::string> &exactSubscriptions) override;
    void stop() override;
    bool publish(const SignalingPublish &message) override;

private:
    DirectRuntimeConfig config_;
    std::unique_ptr<MqttAsyncTransport> transport_;
    StateHandler stateHandler_;
    MessageHandler messageHandler_;
};

} // namespace rtmp::p2p
