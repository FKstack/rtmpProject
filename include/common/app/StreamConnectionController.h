#pragma once

#include <QObject>
#include <QStringList>

#include <memory>

#include "app/ConnectionBindingRegistry.h"
#include "app/ControlMediaObservation.h"
#include "media/PlaybackTypes.h"
#include "server/MediaServerTypes.h"
#include "device_control/DeviceControlTypes.h"

class MainWindow;
class LogManager;
class MultiStreamPlaybackManager;
class UserMessageService;
class VideoWidget;
class ConnectionEventReporter;

struct StreamEventObservation
{
    StreamId streamId = kInvalidStreamId;
    QString localResourceId;
    QString deviceId;
    QString displayName;
    DeviceStatus playbackStatus = DeviceStatus::Disconnected;
    bool removing = false;
};

Q_DECLARE_METATYPE(StreamEventObservation)

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
    ~StreamConnectionController() override;

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
    [[nodiscard]] StreamId selectedControlStreamId() const noexcept;
    [[nodiscard]] ControlMediaObservation controlMediaObservation(
        StreamId streamId
    ) const;
    [[nodiscard]] static QString deviceIdFromRtmpUrl(const QString &streamUrl);
    [[nodiscard]] static QString stableEventResourceId(
        const QString &cameraId,
        const QString &deviceId,
        const QString &streamUrl
    );

public slots:
    void setDevicePresence(const QString &deviceId, DevicePresenceState state);
signals:
    void connectionRemoved(StreamId streamId, const QString &url);
    void deviceBound(const QString &deviceId);
    void deviceUnbound(const QString &deviceId);
    void controlTargetChanged(StreamId streamId, const QString &deviceId,
                              const QString &streamUrl);
    void controlTargetMediaChanged(StreamId streamId);
    void streamEventObserved(const StreamEventObservation &observation);
    void streamRemovedObserved(const StreamEventObservation &observation);

private:
    void showConnectionDialog();
    void connectVideoWidget(ConnectionBinding &binding);
    void toggleAudio(VideoWidget *videoWidget);
    void selectControlTarget(StreamId streamId);
    [[nodiscard]] StreamEventObservation streamEventObservation(
        const ConnectionBinding &binding
    ) const;
    void publishStreamEventObservation(const ConnectionBinding &binding);

    MainWindow *mainWindow_ = nullptr;
    MultiStreamPlaybackManager *playbackManager_ = nullptr;
    std::unique_ptr<ConnectionEventReporter> eventReporter_;
    MediaServerEndpoint mediaServerEndpoint_;
    bool hasMediaServerEndpoint_ = false;
    ConnectionBindingRegistry bindings_;
    StreamId selectedControlStreamId_ = kInvalidStreamId;
};
