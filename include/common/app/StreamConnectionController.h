#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <vector>

#include "media/PlaybackTypes.h"

class MainWindow;
class MultiStreamPlaybackManager;
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
        QObject *parent = nullptr
    );

    StreamId addConnection(
        const QString &displayName,
        const QString &rtmpUrl,
        bool startImmediately = true
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
    };

    void showConnectionDialog();
    void connectVideoWidget(Binding &binding);
    void updateVideoWidgetState(
        VideoWidget *videoWidget,
        int playbackState
    );
    [[nodiscard]] Binding *bindingFor(StreamId streamId) noexcept;
    [[nodiscard]] const Binding *bindingFor(StreamId streamId) const noexcept;
    [[nodiscard]] Binding *bindingFor(VideoWidget *videoWidget) noexcept;
    [[nodiscard]] int nextAvailableCameraNumber() const;

    MainWindow *mainWindow_ = nullptr;
    MultiStreamPlaybackManager *playbackManager_ = nullptr;
    std::vector<Binding> bindings_;
};
