#pragma once

#include "mqtt_transport/MqttTransportTypes.h"

#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QTimer>

#include <atomic>

class MqttTransportCallbackAccess;

/** The single reusable Paho MQTTAsync owner used by control and signaling. */
class MqttAsyncTransport final : public QObject
{
    Q_OBJECT
public:
    explicit MqttAsyncTransport(QObject *parent = nullptr);
    ~MqttAsyncTransport() override;
    MqttAsyncTransport(const MqttAsyncTransport &) = delete;
    MqttAsyncTransport &operator=(const MqttAsyncTransport &) = delete;

    [[nodiscard]] MqttTransportSnapshot snapshot() const;
    [[nodiscard]] bool publish(const MqttPublishRequest &request);

public slots:
    void connectToBroker(const MqttTransportConfig &config,
                         const QList<MqttSubscription> &subscriptions);
    void disconnectFromBroker();

signals:
    void stateChanged(MqttTransportState state, const QString &detail);
    void messageReceived(const MqttInboundMessage &message);
    void messagesDropped(quint64 count);
    void publishAccepted();
    void publishFailed(const QString &detail);

private:
    friend class MqttTransportCallbackAccess;
    struct PendingMessage { std::uint64_t generation; MqttInboundMessage value; };
    void setState(MqttTransportState state, const QString &detail = {});
    void beginSubscription(std::uint64_t generation);
    void handleConnected(std::uint64_t generation);
    void handleSubscribeSuccess(std::uint64_t generation,
                                const QList<int> &grantedQos);
    void handleFailure(std::uint64_t generation, QString detail,
                       bool subscriptionFailure);
    void enqueue(std::uint64_t generation, MqttInboundMessage message);
    void drain(std::uint64_t generation);
    void clearQueue();
    void destroySession();

    void *client_ = nullptr;
    void *callbackContext_ = nullptr;
    MqttTransportConfig config_;
    QList<MqttSubscription> subscriptions_;
    MqttTransportState state_ = MqttTransportState::Disconnected;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> connectionEpoch_{0};
    std::atomic_bool shuttingDown_{false};
    QTimer subscribeTimer_;
    mutable QMutex queueMutex_;
    QQueue<PendingMessage> queue_;
    bool drainScheduled_ = false;
    quint64 droppedMessages_ = 0;
};
