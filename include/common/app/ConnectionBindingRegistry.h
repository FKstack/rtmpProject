#pragma once

#include <QPointer>
#include <QSet>
#include <QString>

#include <vector>

#include "logging/UserMessageTypes.h"
#include "media/PlaybackTypes.h"

class VideoWidget;

struct ConnectionBinding
{
    StreamId streamId = kInvalidStreamId;
    QString displayName;
    QString url;
    QPointer<VideoWidget> videoWidget;
    UserFailureReason lastFailureReason = UserFailureReason::None;
    bool removing = false;
    QString deviceId;
    QString cameraId;
    DeviceStatus playbackStatus = DeviceStatus::Disconnected;
};

/** @brief Session-local registry for stable StreamId/UI/profile bindings. */
class ConnectionBindingRegistry final
{
public:
    static constexpr int kMaximumBindings = 16;

    [[nodiscard]] int size() const noexcept;
    [[nodiscard]] bool isFull() const noexcept;
    [[nodiscard]] bool containsNameOrUrl(
        const QString &displayName,
        const QString &url
    ) const;
    [[nodiscard]] bool containsCameraId(const QString &cameraId) const;
    [[nodiscard]] bool containsDeviceId(const QString &deviceId) const;

    ConnectionBinding &add(ConnectionBinding binding);
    bool remove(StreamId streamId);
    [[nodiscard]] ConnectionBinding *find(StreamId streamId) noexcept;
    [[nodiscard]] const ConnectionBinding *find(StreamId streamId) const noexcept;
    [[nodiscard]] ConnectionBinding *find(VideoWidget *videoWidget) noexcept;
    [[nodiscard]] ConnectionBinding *findDeviceId(const QString &deviceId) noexcept;
    [[nodiscard]] StreamId streamIdFor(const VideoWidget *videoWidget) const noexcept;
    [[nodiscard]] int nextAvailableCameraNumber() const;
    [[nodiscard]] QSet<QString> names() const;
    [[nodiscard]] QSet<QString> urls() const;
    [[nodiscard]] const std::vector<ConnectionBinding> &entries() const noexcept;

private:
    std::vector<ConnectionBinding> bindings_;
};
