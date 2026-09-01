#include "mqtt_signaling/MqttSignalingChannel.h"

#include "mqtt_transport/MqttAsyncTransport.h"

namespace rtmp::p2p {
namespace {
SignalingChannelState channelState(MqttTransportState state)
{
    switch (state) {
    case MqttTransportState::Disabled: return SignalingChannelState::Disabled;
    case MqttTransportState::Disconnected: return SignalingChannelState::Stopped;
    case MqttTransportState::Connecting: return SignalingChannelState::Connecting;
    case MqttTransportState::Subscribing: return SignalingChannelState::Subscribing;
    case MqttTransportState::Ready: return SignalingChannelState::Ready;
    case MqttTransportState::Reconnecting: return SignalingChannelState::Reconnecting;
    case MqttTransportState::Error: return SignalingChannelState::Error;
    }
    return SignalingChannelState::Error;
}
}

MqttSignalingChannel::MqttSignalingChannel(DirectRuntimeConfig config,
                                           QObject *parent)
    : QObject(parent), config_(std::move(config)),
      transport_(std::make_unique<MqttAsyncTransport>())
{
    connect(transport_.get(), &MqttAsyncTransport::stateChanged, this,
            [this](MqttTransportState state, const QString &detail) {
                if (stateHandler_)
                    stateHandler_(channelState(state), detail.toStdString());
            });
    connect(transport_.get(), &MqttAsyncTransport::messageReceived, this,
            [this](const MqttInboundMessage &message) {
                if (!messageHandler_) return;
                SignalingFrame frame;
                frame.topic = message.topic.toStdString();
                frame.payload.assign(message.payload.constData(),
                                     static_cast<std::size_t>(message.payload.size()));
                frame.qos = message.qos;
                frame.duplicate = message.duplicate;
                frame.retained = message.retained;
                frame.expirySeconds = message.expirySeconds < 0
                    ? 0U : static_cast<std::uint32_t>(message.expirySeconds);
                frame.connectionEpoch = message.connectionEpoch;
                frame.receivedAtUnixMs = message.receivedAtMs;
                messageHandler_(frame);
            });
}

MqttSignalingChannel::~MqttSignalingChannel() = default;

void MqttSignalingChannel::setStateHandler(StateHandler handler)
{ stateHandler_ = std::move(handler); }
void MqttSignalingChannel::setMessageHandler(MessageHandler handler)
{ messageHandler_ = std::move(handler); }

bool MqttSignalingChannel::start(
    const std::vector<std::string> &exactSubscriptions)
{
    if (!config_.enabled || config_.brokerUrl.isEmpty()
        || config_.clientId.isEmpty() || exactSubscriptions.empty()) return false;
    QList<MqttSubscription> subscriptions;
    for (const std::string &topic : exactSubscriptions) {
        if (topic.empty() || topic.find('#') != std::string::npos
            || topic.find('+') != std::string::npos) return false;
        subscriptions.push_back({QString::fromStdString(topic), 1, true, 0});
    }
    MqttTransportConfig transport;
    transport.enabled = true;
    transport.brokerUrl = config_.brokerUrl;
    transport.clientId = config_.clientId;
    transport.protocol = MqttTransportProtocol::V5;
    transport.automaticReconnect = true;
    transport.maximumPayloadBytes = static_cast<qsizetype>(kMaxEnvelopeBytes);
    transport.maximumPendingMessages = 128;
    transport.truncateOversizePayload = false;
    transport.overflowPolicy = MqttTransportOverflowPolicy::FailConnection;
    transport_->connectToBroker(transport, subscriptions);
    return true;
}

void MqttSignalingChannel::stop()
{
    transport_->disconnectFromBroker();
}

bool MqttSignalingChannel::publish(const SignalingPublish &message)
{
    if (message.topic.empty() || message.topic.find('#') != std::string::npos
        || message.topic.find('+') != std::string::npos
        || message.payload.size() > kMaxEnvelopeBytes) return false;
    MqttPublishRequest request;
    request.topic = QString::fromStdString(message.topic);
    request.payload = QByteArray(message.payload.data(),
                                 static_cast<qsizetype>(message.payload.size()));
    request.qos = message.qos;
    request.retained = message.retained;
    request.expirySeconds = message.expirySeconds;
    return transport_->publish(request);
}

} // namespace rtmp::p2p
