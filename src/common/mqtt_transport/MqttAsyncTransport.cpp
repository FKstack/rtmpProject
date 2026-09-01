#include "mqtt_transport/MqttAsyncTransport.h"

#include <MQTTAsync.h>
#include <MQTTProperties.h>

#include <QDateTime>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSemaphore>
#include <QUrl>
#include <QUuid>

#include <cstring>

namespace {
MQTTAsync asClient(void *value) { return static_cast<MQTTAsync>(value); }
struct CallbackContext { QPointer<MqttAsyncTransport> owner; std::uint64_t generation; };
struct DisconnectContext { QSemaphore completion; };
CallbackContext *callback(void *value) { return static_cast<CallbackContext *>(value); }

QString uriFor(const QString &value)
{
    const QUrl url(value);
    return QStringLiteral("tcp://%1:%2").arg(url.host()).arg(url.port(1883));
}

QString validConfig(const MqttTransportConfig &config,
                    const QList<MqttSubscription> &subscriptions)
{
    if (!config.enabled) return {};
    const QUrl url(config.brokerUrl, QUrl::StrictMode);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("mqtt"), Qt::CaseInsensitive) != 0
        || url.host().isEmpty() || !url.userInfo().isEmpty() || !url.path().isEmpty()
        || url.hasQuery() || url.hasFragment()
        || url.port(1883) < 1 || url.port(1883) > 65535)
        return QStringLiteral("mqtt_transport_invalid_broker");
    if (config.clientId.trimmed().isEmpty() || config.clientId.size() > 256
        || config.keepAliveSeconds < 1 || config.maximumPayloadBytes < 1
        || config.maximumPendingMessages < 1)
        return QStringLiteral("mqtt_transport_invalid_config");
    for (const auto &item : subscriptions) {
        if (item.topic.isEmpty() || item.topic.contains(QLatin1Char('#'))
            || item.topic.contains(QLatin1Char('+')) || item.topic.contains(QChar::Null)
            || item.qos < 0 || item.qos > 2 || item.retainHandling < 0
            || item.retainHandling > 2)
            return QStringLiteral("mqtt_transport_invalid_subscription");
    }
    return {};
}
}

MqttAsyncTransport::MqttAsyncTransport(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<MqttTransportState>();
    qRegisterMetaType<MqttInboundMessage>();
    subscribeTimer_.setSingleShot(true);
    subscribeTimer_.setInterval(5000);
    connect(&subscribeTimer_, &QTimer::timeout, this, [this] {
        handleFailure(generation_, QStringLiteral("mqtt_suback_timeout"), true);
    });
}

MqttAsyncTransport::~MqttAsyncTransport()
{
    shuttingDown_ = true;
    disconnectFromBroker();
}

MqttTransportSnapshot MqttAsyncTransport::snapshot() const
{
    QMutexLocker lock(&queueMutex_);
    return {state_, generation_.load(), connectionEpoch_.load(), droppedMessages_};
}

void MqttAsyncTransport::connectToBroker(
    const MqttTransportConfig &config, const QList<MqttSubscription> &subscriptions)
{
    const QString error = validConfig(config, subscriptions);
    if (!error.isEmpty()) { setState(MqttTransportState::Error, error); return; }
    disconnectFromBroker();
    config_ = config;
    subscriptions_ = subscriptions;
    if (!config.enabled) { setState(MqttTransportState::Disabled); return; }
    const std::uint64_t generation = ++generation_;
    const QByteArray uri = uriFor(config.brokerUrl).toUtf8();
    const QByteArray id = config.clientId.toUtf8();
    MQTTAsync handle = nullptr;
    int code = MQTTASYNC_FAILURE;
    if (config.protocol == MqttTransportProtocol::V5) {
        MQTTAsync_createOptions createOptions = MQTTAsync_createOptions_initializer5;
        createOptions.sendWhileDisconnected = 0;
        createOptions.maxBufferedMessages = 32;
        code = MQTTAsync_createWithOptions(&handle, uri.constData(), id.constData(),
            MQTTCLIENT_PERSISTENCE_NONE, nullptr, &createOptions);
    } else {
        code = MQTTAsync_create(&handle, uri.constData(), id.constData(),
                                MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    }
    if (code != MQTTASYNC_SUCCESS) {
        setState(MqttTransportState::Error,
                 QStringLiteral("mqtt_create_failed:%1").arg(code));
        return;
    }
    client_ = handle;
    auto *context = new CallbackContext{this, generation};
    callbackContext_ = context;
    MQTTAsync_setConnected(handle, context, &MqttAsyncTransport::onConnected);
    MQTTAsync_setCallbacks(handle, context, &MqttAsyncTransport::onConnectionLost,
        reinterpret_cast<MQTTAsync_messageArrived *>(&MqttAsyncTransport::onMessageArrived), nullptr);
    MQTTAsync_connectOptions options = MQTTAsync_connectOptions_initializer;
    options.keepAliveInterval = config.keepAliveSeconds;
    options.automaticReconnect = config.automaticReconnect ? 1 : 0;
    options.minRetryInterval = 1;
    options.maxRetryInterval = 30;
    options.context = context;
    if (config.protocol == MqttTransportProtocol::V5) {
        options.cleansession = 0;
        options.cleanstart = 1;
        options.MQTTVersion = MQTTVERSION_5;
        options.onFailure5 = reinterpret_cast<MQTTAsync_onFailure5 *>(&MqttAsyncTransport::onConnectFailure5);
    } else {
        options.cleansession = 1;
        options.onFailure = reinterpret_cast<MQTTAsync_onFailure *>(&MqttAsyncTransport::onConnectFailure);
    }
    setState(MqttTransportState::Connecting);
    code = MQTTAsync_connect(handle, &options);
    if (code != MQTTASYNC_SUCCESS) {
        destroySession();
        setState(MqttTransportState::Error,
                 QStringLiteral("mqtt_connect_submit_failed:%1").arg(code));
    }
}

void MqttAsyncTransport::disconnectFromBroker()
{
    ++generation_;
    subscribeTimer_.stop();
    clearQueue();
    if (client_ != nullptr) {
        MQTTAsync handle = asClient(client_);
        if (MQTTAsync_isConnected(handle)) {
            auto *context = new DisconnectContext;
            MQTTAsync_disconnectOptions options = MQTTAsync_disconnectOptions_initializer;
            options.timeout = 500;
            options.context = context;
            options.onSuccess = reinterpret_cast<MQTTAsync_onSuccess *>(&MqttAsyncTransport::onDisconnectSuccess);
            options.onFailure = reinterpret_cast<MQTTAsync_onFailure *>(&MqttAsyncTransport::onDisconnectFailure);
            if (MQTTAsync_disconnect(handle, &options) == MQTTASYNC_SUCCESS)
                context->completion.tryAcquire(1, 600);
            destroySession();
            delete context;
        } else destroySession();
    }
    if (!shuttingDown_) setState(MqttTransportState::Disconnected);
}

bool MqttAsyncTransport::publish(const MqttPublishRequest &request)
{
    if (client_ == nullptr || state_ != MqttTransportState::Ready
        || !MQTTAsync_isConnected(asClient(client_)) || request.topic.isEmpty()
        || request.topic.contains(QLatin1Char('#')) || request.topic.contains(QLatin1Char('+'))
        || request.payload.size() > config_.maximumPayloadBytes || request.qos < 0
        || request.qos > 2) {
        emit publishFailed(QStringLiteral("mqtt_publish_not_ready_or_invalid"));
        return false;
    }
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = const_cast<char *>(request.payload.constData());
    message.payloadlen = request.payload.size();
    message.qos = request.qos;
    message.retained = request.retained ? 1 : 0;
    if (config_.protocol == MqttTransportProtocol::V5 && request.expirySeconds > 0) {
        MQTTProperty property;
        std::memset(&property, 0, sizeof(property));
        property.identifier = MQTTPROPERTY_CODE_MESSAGE_EXPIRY_INTERVAL;
        property.value.integer4 = request.expirySeconds;
        MQTTProperties_add(&message.properties, &property);
    }
    MQTTAsync_responseOptions response = MQTTAsync_responseOptions_initializer;
    const QByteArray topic = request.topic.toUtf8();
    const int code = MQTTAsync_sendMessage(asClient(client_), topic.constData(), &message, &response);
    MQTTProperties_free(&message.properties);
    if (code != MQTTASYNC_SUCCESS) {
        emit publishFailed(QStringLiteral("mqtt_publish_submit_failed:%1").arg(code));
        return false;
    }
    emit publishAccepted();
    return true;
}

void MqttAsyncTransport::setState(MqttTransportState state, const QString &detail)
{
    if (state_ == state && detail.isEmpty()) return;
    state_ = state;
    emit stateChanged(state, detail);
}

void MqttAsyncTransport::handleConnected(std::uint64_t generation)
{
    if (generation != generation_ || shuttingDown_) return;
    ++connectionEpoch_;
    beginSubscription(generation);
}

void MqttAsyncTransport::beginSubscription(std::uint64_t generation)
{
    if (generation != generation_ || client_ == nullptr) return;
    if (subscriptions_.isEmpty()) { setState(MqttTransportState::Ready); return; }
    setState(MqttTransportState::Subscribing);
    QList<QByteArray> encoded;
    QList<char *> topics;
    QList<int> qos;
    QList<MQTTSubscribe_options> subscribeOptions;
    for (const auto &item : subscriptions_) {
        encoded.push_back(item.topic.toUtf8());
        topics.push_back(encoded.back().data());
        qos.push_back(item.qos);
        MQTTSubscribe_options value = MQTTSubscribe_options_initializer;
        value.noLocal = item.noLocal ? 1 : 0;
        value.retainHandling = item.retainHandling;
        subscribeOptions.push_back(value);
    }
    MQTTAsync_responseOptions response = MQTTAsync_responseOptions_initializer;
    response.context = callbackContext_;
    if (config_.protocol == MqttTransportProtocol::V5) {
        response.onSuccess5 = reinterpret_cast<MQTTAsync_onSuccess5 *>(&MqttAsyncTransport::onSubscribeSuccess5);
        response.onFailure5 = reinterpret_cast<MQTTAsync_onFailure5 *>(&MqttAsyncTransport::onSubscribeFailure5);
        response.subscribeOptionsCount = subscribeOptions.size();
        response.subscribeOptionsList = subscribeOptions.data();
    } else {
        response.onSuccess = reinterpret_cast<MQTTAsync_onSuccess *>(&MqttAsyncTransport::onSubscribeSuccess);
        response.onFailure = reinterpret_cast<MQTTAsync_onFailure *>(&MqttAsyncTransport::onSubscribeFailure);
    }
    const int code = MQTTAsync_subscribeMany(asClient(client_), topics.size(), topics.data(), qos.data(), &response);
    if (code != MQTTASYNC_SUCCESS) {
        handleFailure(generation, QStringLiteral("mqtt_subscribe_submit_failed:%1").arg(code), true);
        return;
    }
    subscribeTimer_.start();
}

void MqttAsyncTransport::handleSubscribeSuccess(std::uint64_t generation,
                                                const QList<int> &qos)
{
    if (generation != generation_ || qos.size() != subscriptions_.size()) return;
    subscribeTimer_.stop();
    for (int value : qos) if (value < 0 || value > 2) {
        handleFailure(generation, QStringLiteral("mqtt_suback_rejected"), true); return;
    }
    setState(MqttTransportState::Ready);
}

void MqttAsyncTransport::handleFailure(std::uint64_t generation, QString detail,
                                       bool subscriptionFailure)
{
    if (generation != generation_ || shuttingDown_) return;
    subscribeTimer_.stop();
    if (subscriptionFailure) { ++generation_; destroySession(); setState(MqttTransportState::Error, detail); }
    else setState(MqttTransportState::Reconnecting, detail);
}

void MqttAsyncTransport::enqueue(std::uint64_t generation, MqttInboundMessage message)
{
    bool schedule = false;
    bool fail = false;
    {
        QMutexLocker lock(&queueMutex_);
        if (generation != generation_ || shuttingDown_) return;
        if (message.originalPayloadSize > config_.maximumPayloadBytes
            && !config_.truncateOversizePayload) {
            ++droppedMessages_;
            fail = config_.overflowPolicy == MqttTransportOverflowPolicy::FailConnection;
        } else {
            while (queue_.size() >= config_.maximumPendingMessages) {
                if (config_.overflowPolicy == MqttTransportOverflowPolicy::FailConnection) { fail = true; break; }
                queue_.dequeue(); ++droppedMessages_;
            }
            if (!fail) queue_.enqueue({generation, std::move(message)});
        }
        if (!drainScheduled_) { drainScheduled_ = true; schedule = true; }
    }
    QPointer<MqttAsyncTransport> owner(this);
    if (fail) QMetaObject::invokeMethod(this, [owner, generation] {
        if (owner) owner->handleFailure(generation, QStringLiteral("slow_consumer"), true);
    }, Qt::QueuedConnection);
    else if (schedule) QMetaObject::invokeMethod(this, [owner, generation] {
        if (owner) owner->drain(generation);
    }, Qt::QueuedConnection);
}

void MqttAsyncTransport::drain(std::uint64_t generation)
{
    QQueue<PendingMessage> pending;
    quint64 dropped = 0;
    { QMutexLocker lock(&queueMutex_); pending.swap(queue_); dropped = droppedMessages_; droppedMessages_ = 0; drainScheduled_ = false; }
    if (generation != generation_ || shuttingDown_) return;
    if (dropped) emit messagesDropped(dropped);
    while (!pending.isEmpty()) emit messageReceived(pending.dequeue().value);
}

void MqttAsyncTransport::clearQueue()
{
    QMutexLocker lock(&queueMutex_);
    queue_.clear(); droppedMessages_ = 0; drainScheduled_ = false;
}

void MqttAsyncTransport::destroySession()
{
    if (client_ != nullptr) { MQTTAsync handle = asClient(client_); MQTTAsync_destroy(&handle); client_ = nullptr; }
    delete callback(callbackContext_); callbackContext_ = nullptr;
}

void MqttAsyncTransport::onConnected(void *raw, char *)
{
    auto *ctx = callback(raw); if (!ctx || ctx->owner.isNull()) return;
    const auto owner = ctx->owner; const auto generation = ctx->generation;
    QMetaObject::invokeMethod(owner, [owner, generation] { if (owner) owner->handleConnected(generation); }, Qt::QueuedConnection);
}
void MqttAsyncTransport::onConnectionLost(void *raw, char *)
{
    auto *ctx = callback(raw); if (!ctx || ctx->owner.isNull()) return;
    const auto owner = ctx->owner; const auto generation = ctx->generation;
    QMetaObject::invokeMethod(owner, [owner, generation] { if (owner) owner->handleFailure(generation, QStringLiteral("mqtt_connection_lost"), false); }, Qt::QueuedConnection);
}
void MqttAsyncTransport::onConnectFailure(void *raw, void *) { onConnectionLost(raw, nullptr); }
void MqttAsyncTransport::onConnectFailure5(void *raw, void *) { onConnectionLost(raw, nullptr); }
void MqttAsyncTransport::onSubscribeSuccess(void *raw, void *value)
{
    auto *ctx = callback(raw); auto *response = static_cast<MQTTAsync_successData *>(value);
    if (!ctx || ctx->owner.isNull()) return;
    QList<int> qos;
    for (int i = 0; i < ctx->owner->subscriptions_.size(); ++i)
        qos.push_back(response && response->alt.qosList ? response->alt.qosList[i] : -1);
    const auto owner = ctx->owner; const auto generation = ctx->generation;
    QMetaObject::invokeMethod(owner, [owner, generation, qos] { if (owner) owner->handleSubscribeSuccess(generation, qos); }, Qt::QueuedConnection);
}
void MqttAsyncTransport::onSubscribeSuccess5(void *raw, void *value)
{
    auto *ctx = callback(raw); auto *response = static_cast<MQTTAsync_successData5 *>(value);
    if (!ctx || ctx->owner.isNull()) return;
    QList<int> qos;
    for (int i = 0; i < ctx->owner->subscriptions_.size(); ++i) {
        const bool available = response != nullptr
            && response->alt.sub.reasonCodes != nullptr
            && i < response->alt.sub.reasonCodeCount;
        qos.push_back(available
            ? static_cast<int>(response->alt.sub.reasonCodes[i]) : -1);
    }
    const auto owner = ctx->owner; const auto generation = ctx->generation;
    QMetaObject::invokeMethod(owner, [owner, generation, qos] { if (owner) owner->handleSubscribeSuccess(generation, qos); }, Qt::QueuedConnection);
}
void MqttAsyncTransport::onSubscribeFailure(void *raw, void *)
{
    auto *ctx = callback(raw); if (!ctx || ctx->owner.isNull()) return; const auto owner=ctx->owner; const auto generation=ctx->generation;
    QMetaObject::invokeMethod(owner, [owner,generation]{ if(owner) owner->handleFailure(generation,QStringLiteral("mqtt_subscribe_failed"),true); }, Qt::QueuedConnection);
}
void MqttAsyncTransport::onSubscribeFailure5(void *raw, void *value) { onSubscribeFailure(raw, value); }
int MqttAsyncTransport::onMessageArrived(void *raw, char *topicName, int topicLength, void *rawMessage)
{
    auto *ctx = callback(raw); auto *message = static_cast<MQTTAsync_message *>(rawMessage);
    const int length = topicLength > 0 ? topicLength : (topicName ? static_cast<int>(std::strlen(topicName)) : 0);
    MqttInboundMessage result; result.topic = QString::fromUtf8(topicName, length); result.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
    if (message) {
        result.originalPayloadSize = message->payloadlen; result.qos = message->qos; result.duplicate = message->dup != 0; result.retained = message->retained != 0;
        if (message->payload && message->payloadlen > 0 && ctx
            && !ctx->owner.isNull()) {
            const qsizetype retainedBytes = qMin<qsizetype>(
                message->payloadlen, ctx->owner->config_.maximumPayloadBytes);
            result.payload = QByteArray(
                static_cast<const char *>(message->payload), retainedBytes);
        }
        result.expirySeconds = MQTTProperties_getNumericValue(&message->properties, MQTTPROPERTY_CODE_MESSAGE_EXPIRY_INTERVAL);
        if (ctx) result.connectionEpoch = ctx->owner ? ctx->owner->connectionEpoch_.load() : 0;
    }
    if (ctx && !ctx->owner.isNull()) ctx->owner->enqueue(ctx->generation, std::move(result));
    MQTTAsync_freeMessage(&message); MQTTAsync_free(topicName); return 1;
}
void MqttAsyncTransport::onDisconnectSuccess(void *raw, void *) { if (auto *ctx=static_cast<DisconnectContext *>(raw)) ctx->completion.release(); }
void MqttAsyncTransport::onDisconnectFailure(void *raw, void *) { onDisconnectSuccess(raw,nullptr); }
