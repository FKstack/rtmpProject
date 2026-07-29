#pragma once

#include <QObject>

#include <unordered_map>

#include "logging/UserMessageTypes.h"

class UserMessageService final : public QObject
{
    Q_OBJECT

public:
    explicit UserMessageService(
        int repeatWindowMs = 60'000,
        QObject *parent = nullptr
    );

    void publish(const UserEvent &event);
    void resetDeviceConnectionFailure(std::uint64_t deviceId);

    [[nodiscard]] static QString messageText(const UserEvent &event);

signals:
    void messageAdded(const UserMessage &message);

private:
    void publishOnOwnerThread(UserEvent event);
    void resetOnOwnerThread(std::uint64_t deviceId);
    [[nodiscard]] static UserMessageKind messageKind(UserEventType type);
    [[nodiscard]] static QString eventKey(const UserEvent &event);

    int repeatWindowMs_ = 60'000;
    std::unordered_map<std::string, qint64> lastEmittedMs_;
};
