#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <vector>

#include "logging/LogTypes.h"
#include "logging/UserMessageTypes.h"
#include "media/PlaybackTypes.h"

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
    bool removeConnection(StreamId streamId, bool askForConfirmation);
    bool preloadUrls(const QStringList &streamUrls);

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
    std::vector<Binding> bindings_;
};
