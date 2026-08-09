#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <vector>

#include "logging/LogTypes.h"
#include "logging/UserMessageTypes.h"
#include "media/PlaybackTypes.h"
#include "server/MediaServerTypes.h"

class MainWindow;
class LogManager;
class MultiStreamPlaybackManager;
class UserMessageService;
class VideoWidget;

/**
 * @brief 在 UI 线程中协调连接对话框、稳定 StreamId、播放器和视频格。
 */
class StreamConnectionController final : public QObject
{
    Q_OBJECT

public:
    StreamConnectionController(
        MainWindow *mainWindow,
        MultiStreamPlaybackManager *playbackManager,
        LogManager *logManager,
        UserMessageService *userMessageService,
        QObject *parent = nullptr
    );

    StreamId addConnection(
        const QString &displayName,
        const QString &rtmpUrl,
        bool startImmediately = true,
        bool userInitiated = false
    );

    /**
     * @brief 按摄像头档案接入一路连接。
     *
     * 播放 URL 由 buildRtmpUrl(endpoint, profile.streamKey) 生成；
     * 生成失败或 cameraId 与会话内既有绑定重复时记 warning 并返回
     * kInvalidStreamId。成功后行为与完整 URL 版本一致，并在会话内
     * 维持 cameraId 到 StreamId 的绑定（不写回 profile）。
     */
    StreamId addConnection(
        const CameraStreamProfile &profile,
        const MediaServerEndpoint &endpoint,
        bool startImmediately = true
    );
    bool removeConnection(StreamId streamId, bool askForConfirmation);
    bool preloadUrls(const QStringList &streamUrls);

    /**
     * @brief 设置媒体服务器接入点。
     *
     * 设置后连接对话框的默认 URL 由 buildRtmpUrl 按接入点生成；
     * --url 预装与手工输入完整 URL 的流程不受影响，优先级更高。
     */
    void setMediaServerEndpoint(const MediaServerEndpoint &endpoint);

    [[nodiscard]] StreamId streamIdFor(
        const VideoWidget *videoWidget
    ) const noexcept;

private:
    struct Binding
    {
        StreamId streamId = kInvalidStreamId;
        QString displayName;
        QString url;
        QPointer<VideoWidget> videoWidget;
        UserFailureReason lastFailureReason = UserFailureReason::None;
        bool removing = false;
        // 仅 profile 接入的连接携带 cameraId；其余连接保持为空。
        QString cameraId;
    };

    void showConnectionDialog();
    void connectVideoWidget(Binding &binding);
    [[nodiscard]] LogContext logContext(const Binding &binding) const;
    void logDeviceState(const Binding &binding, DeviceStatus status);
    void publishUserEvent(
        UserEventType type,
        UserFailureReason reason,
        const Binding *binding,
        const QString &displayName = {}
    );
    void writeAudit(
        AuditAction action,
        AuditResult result,
        const Binding *binding,
        const QString &displayName,
        const QString &reason = {}
    );
    [[nodiscard]] static UserFailureReason userReason(
        PlaybackErrorCode code
    );
    [[nodiscard]] Binding *bindingFor(StreamId streamId) noexcept;
    [[nodiscard]] const Binding *bindingFor(StreamId streamId) const noexcept;
    [[nodiscard]] Binding *bindingFor(VideoWidget *videoWidget) noexcept;
    [[nodiscard]] int nextAvailableCameraNumber() const;

    MainWindow *mainWindow_ = nullptr;
    MultiStreamPlaybackManager *playbackManager_ = nullptr;
    LogManager *logManager_ = nullptr;
    UserMessageService *userMessageService_ = nullptr;
    MediaServerEndpoint mediaServerEndpoint_;
    bool hasMediaServerEndpoint_ = false;
    std::vector<Binding> bindings_;
};
