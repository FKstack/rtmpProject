#include "app/StreamConnectionController.h"

#include <QMessageBox>
#include <QSet>

#include <algorithm>

#include "media/FFmpegPlayer.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/ConnectionDialog.h"
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

StreamConnectionController::StreamConnectionController(
    MainWindow *mainWindow,
    MultiStreamPlaybackManager *playbackManager,
    QObject *parent
)
    : QObject(parent)
    , mainWindow_(mainWindow)
    , playbackManager_(playbackManager)
{
    Q_ASSERT(mainWindow_ != nullptr);
    Q_ASSERT(playbackManager_ != nullptr);

    connect(
        mainWindow_, &MainWindow::addConnectionRequested,
        this, &StreamConnectionController::showConnectionDialog
    );
    connect(
        playbackManager_, &MultiStreamPlaybackManager::frameReady,
        this,
        [this](StreamId streamId, const PresentableVideoFrame &frame) {
            Binding *binding = bindingFor(streamId);
            if (binding != nullptr && binding->videoWidget != nullptr) {
                binding->videoWidget->displayFrame(frame.image);
            }
        }
    );
    connect(
        playbackManager_, &MultiStreamPlaybackManager::stateChanged,
        this,
        [this](StreamId streamId, FFmpegPlayer::PlaybackState state) {
            Binding *binding = bindingFor(streamId);
            if (binding != nullptr) {
                updateVideoWidgetState(
                    binding->videoWidget,
                    static_cast<int>(state)
                );
            }
        }
    );
    connect(
        playbackManager_, &MultiStreamPlaybackManager::errorOccurred,
        this,
        [this](StreamId streamId, const QString &message) {
            Binding *binding = bindingFor(streamId);
            if (binding != nullptr && binding->videoWidget != nullptr) {
                binding->videoWidget->setStatusText(message);
            }
        }
    );
}

StreamId StreamConnectionController::addConnection(
    const QString &displayName,
    const QString &rtmpUrl,
    bool startImmediately
)
{
    if (bindings_.size() >= 16 ||
        displayName.trimmed().isEmpty() ||
        !ConnectionDialog::isValidRtmpUrl(rtmpUrl)) {
        return kInvalidStreamId;
    }
    const QString normalizedName = displayName.trimmed();
    const QString normalizedUrl = rtmpUrl.trimmed();
    const bool duplicate = std::any_of(
        bindings_.begin(), bindings_.end(),
        [&](const Binding &binding) {
            return binding.displayName == normalizedName ||
                   binding.url == normalizedUrl;
        }
    );
    if (duplicate) {
        return kInvalidStreamId;
    }

    const StreamId streamId =
        playbackManager_->addStream(normalizedName, normalizedUrl);
    if (streamId == kInvalidStreamId) {
        return kInvalidStreamId;
    }

    VideoWidget *videoWidget =
        mainWindow_->addConnectionWidget(normalizedName);
    if (videoWidget == nullptr) {
        playbackManager_->removeStream(streamId);
        return kInvalidStreamId;
    }

    bindings_.push_back(
        {streamId, normalizedName, normalizedUrl, videoWidget}
    );
    connectVideoWidget(bindings_.back());
    if (startImmediately) {
        playbackManager_->startStream(streamId);
    }
    return streamId;
}

bool StreamConnectionController::removeConnection(
    StreamId streamId,
    bool askForConfirmation
)
{
    Binding *binding = bindingFor(streamId);
    if (binding == nullptr || binding->videoWidget == nullptr) {
        return false;
    }
    if (askForConfirmation) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            mainWindow_,
            tr("断开并移除"),
            tr("确定断开并移除“%1”吗？").arg(binding->displayName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    VideoWidget *videoWidget = binding->videoWidget;
    if (!playbackManager_->removeStream(streamId)) {
        return false;
    }
    if (!mainWindow_->removeConnectionWidget(videoWidget)) {
        return false;
    }
    bindings_.erase(
        std::remove_if(
            bindings_.begin(),
            bindings_.end(),
            [streamId](const Binding &candidate) {
                return candidate.streamId == streamId;
            }
        ),
        bindings_.end()
    );
    return true;
}

bool StreamConnectionController::preloadUrls(
    const QStringList &streamUrls
)
{
    if (streamUrls.size() > 16) {
        return false;
    }
    for (int index = 0; index < streamUrls.size(); ++index) {
        const QString displayName = QStringLiteral("Camera %1")
                                        .arg(
                                            index + 1,
                                            2,
                                            10,
                                            QLatin1Char('0')
                                        );
        if (addConnection(displayName, streamUrls.at(index), true) ==
            kInvalidStreamId) {
            return false;
        }
    }
    return true;
}

StreamId StreamConnectionController::streamIdFor(
    const VideoWidget *videoWidget
) const noexcept
{
    const auto iterator = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [videoWidget](const Binding &binding) {
            return binding.videoWidget == videoWidget;
        }
    );
    return iterator != bindings_.end()
               ? iterator->streamId
               : kInvalidStreamId;
}

void StreamConnectionController::showConnectionDialog()
{
    if (bindings_.size() >= 16) {
        return;
    }

    const int cameraNumber = nextAvailableCameraNumber();
    const QString displayName = QStringLiteral("Camera %1")
                                    .arg(
                                        cameraNumber,
                                        2,
                                        10,
                                        QLatin1Char('0')
                                    );
    const QString url = QStringLiteral(
        "rtmp://127.0.0.1:1935/live/camera%1"
    ).arg(cameraNumber, 3, 10, QLatin1Char('0'));

    QSet<QString> names;
    QSet<QString> urls;
    for (const Binding &binding : bindings_) {
        names.insert(binding.displayName);
        urls.insert(binding.url);
    }

    ConnectionDialog dialog(mainWindow_);
    dialog.setExistingConnections(names, urls);
    dialog.setDefaults(displayName, url);
    if (dialog.exec() == QDialog::Accepted) {
        addConnection(dialog.displayName(), dialog.streamUrl(), true);
    }
}

void StreamConnectionController::connectVideoWidget(Binding &binding)
{
    VideoWidget *videoWidget = binding.videoWidget;
    const StreamId streamId = binding.streamId;
    connect(
        videoWidget, &VideoWidget::reconnectRequested,
        this,
        [this, streamId](VideoWidget *) {
            playbackManager_->restartStream(streamId);
        }
    );
    connect(
        videoWidget, &VideoWidget::removeRequested,
        this,
        [this, streamId](VideoWidget *) {
            removeConnection(streamId, true);
        }
    );
    connect(
        videoWidget, &VideoWidget::presentationTargetChanged,
        this,
        [this, streamId](
            VideoWidget *,
            const QSize &viewportSize,
            bool fullscreen
        ) {
            playbackManager_->setPresentationTarget(
                streamId, {viewportSize, fullscreen}
            );
        }
    );
    playbackManager_->setPresentationTarget(
        streamId, {QSize(640, 360), false}
    );
}

void StreamConnectionController::updateVideoWidgetState(
    VideoWidget *videoWidget,
    int playbackState
)
{
    if (videoWidget == nullptr) {
        return;
    }
    switch (static_cast<FFmpegPlayer::PlaybackState>(playbackState)) {
    case FFmpegPlayer::PlaybackState::Stopped:
        videoWidget->clearFrame();
        videoWidget->setStatusText(tr("已停止"));
        break;
    case FFmpegPlayer::PlaybackState::Connecting:
        videoWidget->clearFrame();
        videoWidget->setStatusText(tr("正在连接 RTMP..."));
        break;
    case FFmpegPlayer::PlaybackState::Playing:
        videoWidget->setStatusText(tr("正在缓冲视频帧..."));
        break;
    case FFmpegPlayer::PlaybackState::Reconnecting:
        videoWidget->clearFrame();
        videoWidget->setStatusText(tr("连接中断，正在重连..."));
        break;
    }
}

StreamConnectionController::Binding *
StreamConnectionController::bindingFor(StreamId streamId) noexcept
{
    const auto iterator = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [streamId](const Binding &binding) {
            return binding.streamId == streamId;
        }
    );
    return iterator != bindings_.end() ? &*iterator : nullptr;
}

const StreamConnectionController::Binding *
StreamConnectionController::bindingFor(StreamId streamId) const noexcept
{
    const auto iterator = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [streamId](const Binding &binding) {
            return binding.streamId == streamId;
        }
    );
    return iterator != bindings_.end() ? &*iterator : nullptr;
}

StreamConnectionController::Binding *
StreamConnectionController::bindingFor(VideoWidget *videoWidget) noexcept
{
    const auto iterator = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [videoWidget](const Binding &binding) {
            return binding.videoWidget == videoWidget;
        }
    );
    return iterator != bindings_.end() ? &*iterator : nullptr;
}

int StreamConnectionController::nextAvailableCameraNumber() const
{
    for (int cameraNumber = 1; cameraNumber <= 16; ++cameraNumber) {
        const QString candidate = QStringLiteral("Camera %1")
                                      .arg(
                                          cameraNumber,
                                          2,
                                          10,
                                          QLatin1Char('0')
                                      );
        const bool used = std::any_of(
            bindings_.begin(),
            bindings_.end(),
            [&candidate](const Binding &binding) {
                return binding.displayName == candidate;
            }
        );
        if (!used) {
            return cameraNumber;
        }
    }
    return 16;
}
