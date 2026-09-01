#include "device_control/MqttDeviceClient.h"

#include <QDateTime>
#include <QUrl>
#include <QUuid>

#include "device_control/DeviceCommandCodec.h"
#include "device_control/DeviceIdentity.h"
#include "device_control/MqttSettingsRepository.h"
#include "mqtt_transport/MqttAsyncTransport.h"

namespace {
MqttConnectionState legacyState(MqttTransportState state)
{
    switch (state) {
    case MqttTransportState::Disabled: return MqttConnectionState::Disabled;
    case MqttTransportState::Disconnected:
        return MqttConnectionState::Disconnected;
    case MqttTransportState::Connecting:
        return MqttConnectionState::Connecting;
    case MqttTransportState::Subscribing:
        return MqttConnectionState::Subscribing;
    case MqttTransportState::Ready: return MqttConnectionState::Connected;
    case MqttTransportState::Reconnecting:
        return MqttConnectionState::Reconnecting;
    case MqttTransportState::Error: return MqttConnectionState::Error;
    }
    return MqttConnectionState::Error;
}
}

MqttDeviceClient::MqttDeviceClient(QObject *parent)
    : QObject(parent), transport_(std::make_unique<MqttAsyncTransport>())
{
    connect(transport_.get(), &MqttAsyncTransport::stateChanged, this,
            [this](MqttTransportState state, const QString &detail) {
                handleTransportState(static_cast<int>(state), detail);
            });
    connect(transport_.get(), &MqttAsyncTransport::messageReceived, this,
            [this](const MqttInboundMessage &message) {
                MqttObservedMessage observed;
                observed.topic = message.topic;
                observed.payload = message.payload;
                observed.receivedAtMs = message.receivedAtMs;
                observed.originalPayloadSize = message.originalPayloadSize;
                emit messageReceived(observed);
            });
    connect(transport_.get(), &MqttAsyncTransport::messagesDropped, this,
            &MqttDeviceClient::observedMessagesDropped);
}

MqttDeviceClient::~MqttDeviceClient() = default;

MqttConnectionState MqttDeviceClient::state() const noexcept { return state_; }
MqttConnectionOptions MqttDeviceClient::options() const { return options_; }

void MqttDeviceClient::connectToBroker(const MqttConnectionOptions &options)
{
    QString validationError;
    if (!MqttSettingsRepository::validate(options, &validationError)) {
        setState(MqttConnectionState::Error, validationError);
        return;
    }
    options_ = options;
    MqttTransportConfig config;
    config.enabled = options.enabled;
    config.brokerUrl = options.brokerUrl;
    config.clientId = QStringLiteral("rtmp-monitor-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    config.keepAliveSeconds = options.keepAliveSeconds;
    config.protocol = MqttTransportProtocol::V311;
    config.automaticReconnect = true;
    config.maximumPayloadBytes = kMaximumObservedPayloadBytes;
    config.maximumPendingMessages = kMaximumPendingObservedMessages;
    config.truncateOversizePayload = true;
    config.overflowPolicy = MqttTransportOverflowPolicy::DropOldest;
    transport_->connectToBroker(config,
        {{options.topic.trimmed(), 0, false, 0},
         {options.statusTopic.trimmed(), 0, false, 0}});
}

void MqttDeviceClient::disconnectFromBroker()
{
    transport_->disconnectFromBroker();
}

bool MqttDeviceClient::publish(DeviceCommand command)
{
    if (command == DeviceCommand::StartStream) {
        emit commandFailed(command,
                           QStringLiteral("启动推流前必须选择有效的视频连接。"));
        return false;
    }
    return publishPayload(command, DeviceCommandCodec::encode(
        command, QDateTime::currentMSecsSinceEpoch()));
}

bool MqttDeviceClient::publishStartStream(const QString &streamUrl)
{
    const QUrl url(streamUrl.trimmed(), QUrl::StrictMode);
    if (!url.isValid()
        || url.scheme().compare(QStringLiteral("rtmp"), Qt::CaseInsensitive) != 0
        || url.host().isEmpty()
        || !DeviceIdentity::fromRtmpUrl(streamUrl).has_value()) {
        emit commandFailed(DeviceCommand::StartStream,
                           QStringLiteral("所选视频连接的 RTMP URL 无效。"));
        return false;
    }
    return publishPayload(DeviceCommand::StartStream,
        DeviceCommandCodec::encode(DeviceCommand::StartStream,
                                   QDateTime::currentMSecsSinceEpoch(),
                                   streamUrl.trimmed()));
}

bool MqttDeviceClient::publishPayload(DeviceCommand command,
                                      const QByteArray &payload)
{
    MqttPublishRequest request;
    request.topic = options_.topic.trimmed();
    request.payload = payload;
    request.qos = 0;
    request.retained = false;
    if (!transport_->publish(request)) {
        emit commandFailed(command, QStringLiteral("MQTT 尚未连接或发布失败。"));
        return false;
    }
    emit commandSubmitted(command);
    return true;
}

void MqttDeviceClient::setState(MqttConnectionState state,
                                const QString &detail)
{
    if (state_ == state && detail.isEmpty()) return;
    state_ = state;
    emit stateChanged(state, detail);
}

void MqttDeviceClient::handleTransportState(int state, const QString &detail)
{
    setState(legacyState(static_cast<MqttTransportState>(state)), detail);
}
