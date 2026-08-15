#include "device_control/MqttDeviceClient.h"

#include <MQTTAsync.h>

#include <QDateTime>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSemaphore>
#include <QUrl>
#include <QUuid>

#include <cstring>
#include <utility>

#include "device_control/DeviceCommandCodec.h"
#include "device_control/MqttSettingsRepository.h"

namespace {

MQTTAsync asClient(void *client) { return static_cast<MQTTAsync>(client); }

QString pahoUri(const QString &mqttUrl)
{
    const QUrl url(mqttUrl);
    return QStringLiteral("tcp://%1:%2").arg(url.host()).arg(url.port(1883));
}

struct CallbackContext
{
    QPointer<MqttDeviceClient> owner;
    std::uint64_t generation = 0;
};

struct DisconnectContext
{
    QSemaphore completion;
};

CallbackContext *contextOf(void *value)
{
    return static_cast<CallbackContext *>(value);
}

QString responseError(MQTTAsync_failureData *response, const QString &fallback)
{
    if (response == nullptr) return fallback;
    return QStringLiteral("%1 (code=%2, message=%3)")
        .arg(fallback)
        .arg(response->code)
        .arg(QString::fromUtf8(response->message != nullptr
                                   ? response->message : "unknown"));
}

} // namespace

MqttDeviceClient::MqttDeviceClient(QObject *parent) : QObject(parent)
{
    subscribeTimer_.setSingleShot(true);
    connect(&subscribeTimer_, &QTimer::timeout, this, [this] {
        if (state_ == MqttConnectionState::Subscribing) {
            handleSubscribeFailure(
                generation_, QStringLiteral("MQTT SUBACK 等待超时。"));
        }
    });
}

MqttDeviceClient::~MqttDeviceClient()
{
    shuttingDown_ = true;
    ++generation_;
    subscribeTimer_.stop();
    clearObservedInbox();
    destroySession();
}

MqttConnectionState MqttDeviceClient::state() const noexcept { return state_; }
MqttConnectionOptions MqttDeviceClient::options() const { return options_; }

void MqttDeviceClient::connectToBroker(const MqttConnectionOptions &options)
{
    QString validationError;
    if (!MqttSettingsRepository::validate(options, &validationError)) {
        setState(MqttConnectionState::Error, validationError);
        return;
    }
    disconnectFromBroker();
    options_ = options;
    if (!options.enabled) {
        setState(MqttConnectionState::Disabled);
        return;
    }

    const std::uint64_t generation = ++generation_;
    const QByteArray uri = pahoUri(options.brokerUrl).toUtf8();
    const QByteArray clientId =
        (QStringLiteral("rtmp-monitor-") +
         QUuid::createUuid().toString(QUuid::WithoutBraces)).toUtf8();
    MQTTAsync handle = nullptr;
    const int createCode = MQTTAsync_create(
        &handle, uri.constData(), clientId.constData(),
        MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (createCode != MQTTASYNC_SUCCESS) {
        setState(MqttConnectionState::Error,
                 QStringLiteral("MQTT client 创建失败，code=%1").arg(createCode));
        return;
    }
    client_ = handle;
    auto *context = new CallbackContext{this, generation};
    callbackContext_ = context;
    MQTTAsync_setConnected(handle, context, &MqttDeviceClient::onConnected);
    MQTTAsync_setCallbacks(handle, context, &MqttDeviceClient::onConnectionLost,
                           &MqttDeviceClient::onMessageArrived, nullptr);

    MQTTAsync_connectOptions connectOptions = MQTTAsync_connectOptions_initializer;
    connectOptions.keepAliveInterval = options.keepAliveSeconds;
    connectOptions.cleansession = 1;
    connectOptions.automaticReconnect = 1;
    connectOptions.minRetryInterval = 1;
    connectOptions.maxRetryInterval = 30;
    connectOptions.context = context;
    connectOptions.onFailure = &MqttDeviceClient::onConnectFailure;
    setState(MqttConnectionState::Connecting);
    const int code = MQTTAsync_connect(handle, &connectOptions);
    if (code != MQTTASYNC_SUCCESS) {
        destroySession();
        setState(MqttConnectionState::Error,
                 QStringLiteral("MQTT CONNECT 提交失败，code=%1").arg(code));
    }
}

void MqttDeviceClient::disconnectFromBroker()
{
    ++generation_;
    subscribeTimer_.stop();
    clearObservedInbox();
    if (client_ == nullptr) {
        if (!shuttingDown_) setState(MqttConnectionState::Disconnected);
        return;
    }
    MQTTAsync handle = asClient(client_);
    if (MQTTAsync_isConnected(handle)) {
        auto *disconnectContext = new DisconnectContext;
        MQTTAsync_disconnectOptions options = MQTTAsync_disconnectOptions_initializer;
        options.timeout = 500;
        options.context = disconnectContext;
        options.onSuccess = &MqttDeviceClient::onDisconnectSuccess;
        options.onFailure = &MqttDeviceClient::onDisconnectFailure;
        if (MQTTAsync_disconnect(handle, &options) == MQTTASYNC_SUCCESS) {
            disconnectContext->completion.tryAcquire(1, 600);
        }
        // MQTTAsync_destroy() stops the worker and prevents later callbacks,
        // so the callback context remains alive until the session is gone.
        destroySession();
        delete disconnectContext;
    } else {
        destroySession();
    }
    if (!shuttingDown_) setState(MqttConnectionState::Disconnected);
}

bool MqttDeviceClient::publish(DeviceCommand command)
{
    if (client_ == nullptr || state_ != MqttConnectionState::Connected ||
        !MQTTAsync_isConnected(asClient(client_))) {
        emit commandFailed(command, QStringLiteral("MQTT 尚未连接。"));
        return false;
    }
    const QByteArray payload = DeviceCommandCodec::encode(
        command, QDateTime::currentMSecsSinceEpoch());
    const QByteArray topic = options_.topic.trimmed().toUtf8();
    MQTTAsync_responseOptions response = MQTTAsync_responseOptions_initializer;
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = const_cast<char *>(payload.constData());
    message.payloadlen = payload.size();
    message.qos = 0;
    message.retained = 0;
    const int code = MQTTAsync_sendMessage(asClient(client_), topic.constData(),
                                           &message, &response);
    if (code != MQTTASYNC_SUCCESS) {
        emit commandFailed(command,
            QStringLiteral("MQTT PUBLISH 提交失败，code=%1").arg(code));
        return false;
    }
    emit commandSubmitted(command);
    return true;
}

void MqttDeviceClient::setState(MqttConnectionState state, const QString &detail)
{
    if (state_ == state && detail.isEmpty()) return;
    state_ = state;
    emit stateChanged(state, detail);
}

void MqttDeviceClient::destroySession()
{
    if (client_ != nullptr) {
        MQTTAsync handle = asClient(client_);
        MQTTAsync_destroy(&handle);
        client_ = nullptr;
    }
    delete contextOf(callbackContext_);
    callbackContext_ = nullptr;
}

void MqttDeviceClient::handleConnected(std::uint64_t generation)
{
    if (generation != generation_ || shuttingDown_) return;
    beginSubscription(generation);
}

void MqttDeviceClient::handleConnectionLost(std::uint64_t generation,
                                            QString cause)
{
    if (generation != generation_ || shuttingDown_) return;
    subscribeTimer_.stop();
    setState(MqttConnectionState::Reconnecting,
             cause.isEmpty() ? QStringLiteral("Broker 连接中断。") : cause);
}

void MqttDeviceClient::beginSubscription(std::uint64_t generation)
{
    if (generation != generation_ || shuttingDown_ || client_ == nullptr) return;
    setState(MqttConnectionState::Subscribing);
    MQTTAsync_responseOptions response = MQTTAsync_responseOptions_initializer;
    response.context = callbackContext_;
    response.onSuccess = &MqttDeviceClient::onSubscribeSuccess;
    response.onFailure = &MqttDeviceClient::onSubscribeFailure;
    const QByteArray topic = options_.topic.trimmed().toUtf8();
    const int code = MQTTAsync_subscribe(asClient(client_), topic.constData(), 0,
                                         &response);
    if (code != MQTTASYNC_SUCCESS) {
        handleSubscribeFailure(
            generation,
            QStringLiteral("MQTT SUBSCRIBE 提交失败，code=%1").arg(code));
        return;
    }
    subscribeTimer_.start(kSubscribeTimeoutMs);
}

void MqttDeviceClient::handleSubscribeSuccess(std::uint64_t generation,
                                              int grantedQos)
{
    if (generation != generation_ || shuttingDown_) return;
    subscribeTimer_.stop();
    if (grantedQos < 0 || grantedQos > 2) {
        handleSubscribeFailure(
            generation,
            QStringLiteral("MQTT SUBACK 拒绝订阅，grantedQos=%1")
                .arg(grantedQos));
        return;
    }
    setState(MqttConnectionState::Connected);
}

void MqttDeviceClient::handleSubscribeFailure(std::uint64_t generation,
                                              QString detail)
{
    if (generation != generation_ || shuttingDown_) return;
    subscribeTimer_.stop();
    ++generation_;
    clearObservedInbox();
    destroySession();
    setState(MqttConnectionState::Error, detail);
}

void MqttDeviceClient::enqueueObservedMessage(
    std::uint64_t generation, MqttObservedMessage message)
{
    bool scheduleDrain = false;
    {
        QMutexLocker locker(&observedMutex_);
        // Paho owns this callback thread.  Atomics let it reject a late
        // callback before that old session can occupy the replacement
        // session's single scheduled-drain slot.
        if (generation != generation_.load(std::memory_order_acquire) ||
            shuttingDown_.load(std::memory_order_acquire)) return;
        while (observedInbox_.size() >= kMaximumPendingObservedMessages) {
            observedInbox_.dequeue();
            ++observedDroppedCount_;
        }
        observedInbox_.enqueue({generation, std::move(message)});
        if (!observedDrainScheduled_) {
            observedDrainScheduled_ = true;
            scheduleDrain = true;
        }
    }
    if (!scheduleDrain) return;
    const QPointer<MqttDeviceClient> owner(this);
    QMetaObject::invokeMethod(this, [owner, generation] {
        // A queued drain from a destroyed session must not swap or clear the
        // inbox that may already belong to its replacement session.
        if (owner && generation == owner->generation_)
            owner->drainObservedMessages(generation);
    }, Qt::QueuedConnection);
}

void MqttDeviceClient::drainObservedMessages(std::uint64_t generation)
{
    QQueue<PendingObservedMessage> pending;
    quint64 dropped = 0;
    {
        QMutexLocker locker(&observedMutex_);
        pending.swap(observedInbox_);
        dropped = observedDroppedCount_;
        observedDroppedCount_ = 0;
        observedDrainScheduled_ = false;
    }
    if (generation != generation_ || shuttingDown_) return;
    if (dropped > 0) emit observedMessagesDropped(dropped);
    while (!pending.isEmpty()) {
        PendingObservedMessage item = pending.dequeue();
        if (item.generation == generation_)
            emit messageReceived(item.message);
    }
}

void MqttDeviceClient::clearObservedInbox()
{
    QMutexLocker locker(&observedMutex_);
    observedInbox_.clear();
    observedDroppedCount_ = 0;
    observedDrainScheduled_ = false;
}

void MqttDeviceClient::onConnected(void *raw, char *)
{
    CallbackContext *context = contextOf(raw);
    if (context == nullptr || context->owner.isNull()) return;
    const QPointer<MqttDeviceClient> owner = context->owner;
    const std::uint64_t generation = context->generation;
    QMetaObject::invokeMethod(owner, [owner, generation] {
        if (owner) owner->handleConnected(generation);
    }, Qt::QueuedConnection);
}

void MqttDeviceClient::onConnectionLost(void *raw, char *cause)
{
    CallbackContext *context = contextOf(raw);
    if (context == nullptr || context->owner.isNull()) return;
    const QPointer<MqttDeviceClient> owner = context->owner;
    const std::uint64_t generation = context->generation;
    const QString detail = QString::fromUtf8(cause != nullptr ? cause : "");
    QMetaObject::invokeMethod(owner, [owner, generation, detail] {
        if (owner) owner->handleConnectionLost(generation, detail);
    }, Qt::QueuedConnection);
}

void MqttDeviceClient::onConnectFailure(void *raw, MQTTAsync_failureData *response)
{
    CallbackContext *context = contextOf(raw);
    if (context == nullptr || context->owner.isNull()) return;
    const QPointer<MqttDeviceClient> owner = context->owner;
    const std::uint64_t generation = context->generation;
    const QString detail = responseError(response, QStringLiteral("MQTT 连接失败"));
    QMetaObject::invokeMethod(owner, [owner, generation, detail] {
        if (owner && generation == owner->generation_ && !owner->shuttingDown_)
            owner->setState(MqttConnectionState::Reconnecting, detail);
    }, Qt::QueuedConnection);
}

void MqttDeviceClient::onSubscribeSuccess(void *raw,
                                         MQTTAsync_successData *response)
{
    CallbackContext *context = contextOf(raw);
    if (context == nullptr || context->owner.isNull()) return;
    const QPointer<MqttDeviceClient> owner = context->owner;
    const std::uint64_t generation = context->generation;
    const int grantedQos = response != nullptr ? response->alt.qos : -1;
    QMetaObject::invokeMethod(owner, [owner, generation, grantedQos] {
        if (owner) owner->handleSubscribeSuccess(generation, grantedQos);
    }, Qt::QueuedConnection);
}

void MqttDeviceClient::onSubscribeFailure(void *raw,
                                         MQTTAsync_failureData *response)
{
    CallbackContext *context = contextOf(raw);
    if (context == nullptr || context->owner.isNull()) return;
    const QPointer<MqttDeviceClient> owner = context->owner;
    const std::uint64_t generation = context->generation;
    const QString detail = responseError(
        response, QStringLiteral("MQTT 订阅失败"));
    QMetaObject::invokeMethod(owner, [owner, generation, detail] {
        if (owner) owner->handleSubscribeFailure(generation, detail);
    }, Qt::QueuedConnection);
}

int MqttDeviceClient::onMessageArrived(void *raw, char *topicName,
                                       int topicLength,
                                       MQTTAsync_message *message)
{
    CallbackContext *context = contextOf(raw);
    const int actualTopicLength = topicLength > 0
        ? topicLength
        : (topicName != nullptr ? static_cast<int>(std::strlen(topicName)) : 0);
    MqttObservedMessage observed;
    observed.topic = QString::fromUtf8(topicName, actualTopicLength);
    observed.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
    if (message != nullptr && message->payloadlen > 0 &&
        message->payload != nullptr) {
        observed.originalPayloadSize = message->payloadlen;
        const int retainedBytes = static_cast<int>(qMin<qsizetype>(
            message->payloadlen, kMaximumObservedPayloadBytes));
        observed.payload = QByteArray(static_cast<const char *>(message->payload),
                                      retainedBytes);
    }
    if (context != nullptr && !context->owner.isNull()) {
        context->owner->enqueueObservedMessage(context->generation,
                                                std::move(observed));
    }
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

void MqttDeviceClient::onDisconnectSuccess(void *raw, MQTTAsync_successData *)
{
    if (auto *context = static_cast<DisconnectContext *>(raw); context != nullptr)
        context->completion.release();
}

void MqttDeviceClient::onDisconnectFailure(void *raw, MQTTAsync_failureData *)
{
    if (auto *context = static_cast<DisconnectContext *>(raw); context != nullptr)
        context->completion.release();
}
