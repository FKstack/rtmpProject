#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <cstdint>

enum class UserEventType {
    DeviceConnected,
    DeviceDisconnected,
    DeviceAdded,
    DeviceRemoved,
    DeviceUpdated,
    DeviceConnectFailed,
    DeviceAddFailed,
    DeviceRemoveFailed,
    ManualReconnectStarted,
    OperationIncomplete,
    LoginSucceeded,
    LoginFailed,
    LogoutSucceeded,
    ConfigurationUpdated,
    ServerHealthy,
    ServerUnavailable,
};

enum class UserFailureReason {
    None,
    ConnectionTimeout,
    HostUnavailable,
    AuthenticationFailed,
    DuplicateDevice,
    InvalidConfiguration,
    CapacityReached,
    MediaUnavailable,
    InternalFailure,
    Unknown,
};

enum class UserMessageKind {
    Information,
    Success,
    Warning,
    Error,
};

struct UserEvent
{
    UserEventType type = UserEventType::OperationIncomplete;
    UserFailureReason reason = UserFailureReason::None;
    std::uint64_t deviceId = 0;
    QString deviceName;
};

struct UserMessage
{
    QDateTime timestampUtc;
    UserEventType type = UserEventType::OperationIncomplete;
    UserMessageKind kind = UserMessageKind::Information;
    std::uint64_t deviceId = 0;
    QString text;
};

Q_DECLARE_METATYPE(UserEvent)
Q_DECLARE_METATYPE(UserMessage)
