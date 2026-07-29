#include "logging/UserMessageService.h"

#include <QMetaObject>
#include <QThread>

#include <utility>

UserMessageService::UserMessageService(
    int repeatWindowMs,
    QObject *parent
)
    : QObject(parent)
    , repeatWindowMs_(std::max(0, repeatWindowMs))
{
    qRegisterMetaType<UserEvent>();
    qRegisterMetaType<UserMessage>();
}

void UserMessageService::publish(const UserEvent &event)
{
    if (QThread::currentThread() == thread()) {
        publishOnOwnerThread(event);
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, event] {
            publishOnOwnerThread(event);
        },
        Qt::QueuedConnection
    );
}

void UserMessageService::resetDeviceConnectionFailure(
    std::uint64_t deviceId
)
{
    if (QThread::currentThread() == thread()) {
        resetOnOwnerThread(deviceId);
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, deviceId] {
            resetOnOwnerThread(deviceId);
        },
        Qt::QueuedConnection
    );
}

QString UserMessageService::messageText(const UserEvent &event)
{
    const QString device = event.deviceName.trimmed().isEmpty()
        ? tr("设备")
        : event.deviceName.trimmed();
    switch (event.type) {
    case UserEventType::DeviceConnected:
        return tr("%1 连接成功").arg(device);
    case UserEventType::DeviceDisconnected:
        return tr("%1 已断开连接").arg(device);
    case UserEventType::DeviceAdded:
        return tr("已新增%1").arg(device);
    case UserEventType::DeviceRemoved:
        return tr("已删除%1").arg(device);
    case UserEventType::DeviceUpdated:
        return tr("已修改%1").arg(device);
    case UserEventType::ManualReconnectStarted:
        return tr("正在重新连接%1").arg(device);
    case UserEventType::LoginSucceeded:
        return tr("登录成功");
    case UserEventType::LoginFailed:
        return tr("登录失败，请检查用户名和密码");
    case UserEventType::LogoutSucceeded:
        return tr("已退出登录");
    case UserEventType::ConfigurationUpdated:
        return tr("配置修改成功");
    case UserEventType::DeviceConnectFailed:
        switch (event.reason) {
        case UserFailureReason::ConnectionTimeout:
            return tr("%1 连接超时，请检查网络连接").arg(device);
        case UserFailureReason::HostUnavailable:
            return tr(
                "暂时无法找到%1，请确认设备和本机已连接同一 Wi-Fi"
            ).arg(device);
        case UserFailureReason::AuthenticationFailed:
            return tr("%1 验证失败，请检查设备信息").arg(device);
        case UserFailureReason::MediaUnavailable:
            return tr("无法获取%1画面，请确认设备状态正常").arg(device);
        default:
            return tr("%1 连接失败，请检查网络连接").arg(device);
        }
    case UserEventType::DeviceAddFailed:
        switch (event.reason) {
        case UserFailureReason::DuplicateDevice:
            return tr("该设备已经添加");
        case UserFailureReason::CapacityReached:
            return tr("设备数量已达到上限");
        case UserFailureReason::InvalidConfiguration:
            return tr("设备信息不完整，请检查后重试");
        default:
            return tr("添加设备失败，请检查设备信息后重试");
        }
    case UserEventType::DeviceRemoveFailed:
        return tr("删除设备失败，请稍后重试");
    case UserEventType::OperationIncomplete:
        return tr("操作未完成，请检查网络和设备状态后重试");
    }
    return tr("操作失败，请检查网络和设备状态后重试");
}

void UserMessageService::publishOnOwnerThread(UserEvent event)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const std::string key = eventKey(event).toStdString();
    const qint64 nowMs = now.toMSecsSinceEpoch();
    const auto iterator = lastEmittedMs_.find(key);
    if (repeatWindowMs_ > 0 && iterator != lastEmittedMs_.end() &&
        nowMs - iterator->second < repeatWindowMs_) {
        return;
    }
    lastEmittedMs_[key] = nowMs;
    if (event.type == UserEventType::DeviceConnected) {
        resetOnOwnerThread(event.deviceId);
        lastEmittedMs_[key] = nowMs;
    }
    emit messageAdded(
        {now, event.type, messageKind(event.type), event.deviceId,
         messageText(event)}
    );
}

void UserMessageService::resetOnOwnerThread(std::uint64_t deviceId)
{
    const QString prefix = QStringLiteral("%1|%2|")
                               .arg(deviceId)
                               .arg(
                                   static_cast<int>(
                                       UserEventType::DeviceConnectFailed
                                   )
                               );
    for (auto iterator = lastEmittedMs_.begin();
         iterator != lastEmittedMs_.end();) {
        if (QString::fromStdString(iterator->first).startsWith(prefix)) {
            iterator = lastEmittedMs_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

UserMessageKind UserMessageService::messageKind(UserEventType type)
{
    switch (type) {
    case UserEventType::DeviceConnected:
    case UserEventType::DeviceAdded:
    case UserEventType::DeviceRemoved:
    case UserEventType::DeviceUpdated:
    case UserEventType::LoginSucceeded:
    case UserEventType::LogoutSucceeded:
    case UserEventType::ConfigurationUpdated:
        return UserMessageKind::Success;
    case UserEventType::DeviceDisconnected:
    case UserEventType::ManualReconnectStarted:
        return UserMessageKind::Warning;
    case UserEventType::DeviceConnectFailed:
    case UserEventType::DeviceAddFailed:
    case UserEventType::DeviceRemoveFailed:
    case UserEventType::OperationIncomplete:
    case UserEventType::LoginFailed:
        return UserMessageKind::Error;
    }
    return UserMessageKind::Information;
}

QString UserMessageService::eventKey(const UserEvent &event)
{
    return QStringLiteral("%1|%2|%3")
        .arg(event.deviceId)
        .arg(static_cast<int>(event.type))
        .arg(static_cast<int>(event.reason));
}
