#pragma once

#include <QObject>
#include <QMutex>
#include <QQueue>
#include <QTimer>
#include <MQTTAsync.h>

#include <atomic>
#include <cstdint>

#include "device_control/DeviceControlTypes.h"

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
    static void onConnected(void *context, char *cause);
    static void onConnectionLost(void *context, char *cause);
    static void onConnectFailure(void *context, MQTTAsync_failureData *response);
    static void onSubscribeSuccess(void *context,
                                   MQTTAsync_successData *response);
    static void onSubscribeFailure(void *context,
                                   MQTTAsync_failureData *response);
    static int onMessageArrived(void *context, char *topicName, int topicLength,
                                MQTTAsync_message *message);
    static void onDisconnectSuccess(void *context, MQTTAsync_successData *response);
    static void onDisconnectFailure(void *context, MQTTAsync_failureData *response);
    void setState(MqttConnectionState state, const QString &detail = {});
    void destroySession();
    void handleConnected(std::uint64_t generation);
    void handleConnectionLost(std::uint64_t generation, QString cause);
    void beginSubscription(std::uint64_t generation);
    void handleSubscribeSuccess(std::uint64_t generation, int grantedQos);
    void handleSubscribeFailure(std::uint64_t generation, QString detail);
    void enqueueObservedMessage(std::uint64_t generation,
                                MqttObservedMessage message);
    void drainObservedMessages(std::uint64_t generation);
    void clearObservedInbox();
    bool publishPayload(DeviceCommand command, const QByteArray &payload);

    struct PendingObservedMessage
    {
        std::uint64_t generation = 0;
        MqttObservedMessage message;
    };

    void *client_ = nullptr;
    void *callbackContext_ = nullptr;
    MqttConnectionOptions options_;
    MqttConnectionState state_ = MqttConnectionState::Disconnected;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic_bool shuttingDown_{false};
    QTimer subscribeTimer_;
    QMutex observedMutex_;
    QQueue<PendingObservedMessage> observedInbox_;
    bool observedDrainScheduled_ = false;
    quint64 observedDroppedCount_ = 0;
};
