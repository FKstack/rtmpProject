#include "app/StreamConnectionController.h"

#include "app/ConnectionEventReporter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QMessageBox>
#include <QUrl>
#include <optional>

#include "media/FFmpegPlayer.h"
#include "media/MultiStreamPlaybackManager.h"
#include "device_control/DeviceIdentity.h"
#include "server/RtmpUrlBuilder.h"
#include "ui/ConnectionDialog.h"
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

StreamConnectionController::StreamConnectionController(
    MainWindow *mainWindow,
    MultiStreamPlaybackManager *playbackManager,
    LogManager *logManager,
    UserMessageService *userMessageService,
    QObject *parent
)
    : QObject(parent)
    , mainWindow_(mainWindow)
    , playbackManager_(playbackManager)
    , eventReporter_(std::make_unique<ConnectionEventReporter>(
          logManager, userMessageService
      ))
{
    Q_ASSERT(mainWindow_ != nullptr);
    Q_ASSERT(playbackManager_ != nullptr);

    connect(
        mainWindow_, &MainWindow::addConnectionRequested,
        this, &StreamConnectionController::showConnectionDialog
    );
    connect(
        mainWindow_,
        &MainWindow::audioToggleRequested,
        this,
        &StreamConnectionController::toggleAudio
    );
    connect(
        playbackManager_,
        &MultiStreamPlaybackManager::audioStateChanged,
        this,
        [this](StreamId streamId, AudioPlaybackState state) {
            ConnectionBinding *binding = bindings_.find(streamId);
            if (binding != nullptr && binding->videoWidget != nullptr) {
                mainWindow_->updateAudioState(
                    binding->videoWidget,
                    state,
                    playbackManager_->selectedAudioStream() == streamId
                );
            }
        }
    );
    connect(
        playbackManager_, &MultiStreamPlaybackManager::stateChanged,
        this,
        [this](StreamId streamId, DeviceStatus state) {
            ConnectionBinding *binding = bindings_.find(streamId);
            if (binding != nullptr) {
                binding->playbackStatus = state;
                publishStreamEventObservation(*binding);
                mainWindow_->updateDeviceStatus(
                    binding->videoWidget,
                    state,
                    binding->lastFailureReason
                );
                eventReporter_->logDeviceState(*binding, state);
                if (state == DeviceStatus::Playing) {
                    binding->lastFailureReason =
                        UserFailureReason::None;
                    eventReporter_->publish(
                        UserEventType::DeviceConnected,
                        UserFailureReason::None,
                        binding
                    );
                } else if (
                    state == DeviceStatus::Disconnected &&
                    !binding->removing
                ) {
                    eventReporter_->publish(
                        UserEventType::DeviceDisconnected,
                        UserFailureReason::None,
                        binding
                    );
                }
                if (selectedControlStreamId_ == streamId) {
                    emit controlTargetMediaChanged(streamId);
                }
            }
        }
    );
    connect(
        playbackManager_, &MultiStreamPlaybackManager::errorOccurred,
        this,
        [this](StreamId streamId, const PlaybackError &error) {
            ConnectionBinding *binding = bindings_.find(streamId);
            if (binding != nullptr) {
                binding->playbackStatus = DeviceStatus::Error;
                publishStreamEventObservation(*binding);
                const UserFailureReason mappedReason =
                    ConnectionEventReporter::userReason(error.code);
                if (error.code != PlaybackErrorCode::RetryLimitReached ||
                    binding->lastFailureReason ==
                        UserFailureReason::None) {
                    binding->lastFailureReason = mappedReason;
                }
                mainWindow_->updateDeviceStatus(
                    binding->videoWidget,
                    DeviceStatus::Error,
                    binding->lastFailureReason
                );
                eventReporter_->logSystem(
                    LogLevel::Warning,
                    QStringLiteral("media"),
                    QStringLiteral("stream_error"),
                    error.technicalMessage,
                    {
                        {QStringLiteral("errorCode"), static_cast<int>(error.code)},
                        {QStringLiteral("nativeErrorCode"), error.nativeCode},
                        {QStringLiteral("recoverable"), error.recoverable}
                    },
                    eventReporter_->context(*binding),
                    true
                );
                eventReporter_->publish(
                    UserEventType::DeviceConnectFailed,
                    binding->lastFailureReason,
                        binding
                );
            }
        }
    );
    connect(
        playbackManager_,
        &MultiStreamPlaybackManager::reconnectScheduled,
        this,
        [this](
            StreamId streamId,
            int consecutiveFailures,
            int delayMs
        ) {
            ConnectionBinding *binding = bindings_.find(streamId);
            if (binding == nullptr) {
                return;
            }
            binding->playbackStatus = DeviceStatus::Reconnecting;
            publishStreamEventObservation(*binding);
            mainWindow_->updateDeviceStatus(
                binding->videoWidget,
                DeviceStatus::Reconnecting
            );
            eventReporter_->logSystem(
                LogLevel::Info,
                QStringLiteral("media"),
                QStringLiteral("reconnect_scheduled"),
                QStringLiteral("Automatic reconnect scheduled."),
                {
                    {QStringLiteral("delayMs"), delayMs},
                    {QStringLiteral("consecutiveFailures"), consecutiveFailures}
                },
                eventReporter_->context(*binding),
                true
            );
        }
    );
}
StreamConnectionController::~StreamConnectionController() = default;

StreamId StreamConnectionController::addConnection(
    const QString &displayName,
    const QString &rtmpUrl,
    bool startImmediately,
    bool userInitiated
)
{
    const QString normalizedName = displayName.trimmed();
    const QString normalizedUrl = rtmpUrl.trimmed();
    const auto fail =
        [this, &normalizedName, &normalizedUrl, userInitiated](
            UserFailureReason reason,
            const QString &technicalReason
        ) {
            eventReporter_->logSystem(
                LogLevel::Warning,
                QStringLiteral("device"),
                QStringLiteral("connection_add_failed"),
                technicalReason,
                {{QStringLiteral("failureReason"), static_cast<int>(reason)}},
                {0, normalizedName, normalizedUrl}
            );
            if (userInitiated) {
                eventReporter_->publish(
                    UserEventType::DeviceAddFailed,
                    reason,
                    nullptr,
                    normalizedName
                );
                eventReporter_->audit(
                    AuditAction::AddDevice,
                    AuditResult::Failure,
                    nullptr,
                    normalizedName,
                    technicalReason
                );
            }
            return kInvalidStreamId;
        };

    if (bindings_.isFull()) {
        return fail(
            UserFailureReason::CapacityReached,
            QStringLiteral("Maximum device count reached.")
        );
    }
    if (normalizedName.isEmpty() ||
        !ConnectionDialog::isValidRtmpUrl(normalizedUrl)) {
        return fail(
            UserFailureReason::InvalidConfiguration,
            QStringLiteral("Device name or connection URL is invalid.")
        );
    }
    const QString deviceId = deviceIdFromRtmpUrl(normalizedUrl);
    if (deviceId.isEmpty()) {
        return fail(
            UserFailureReason::InvalidConfiguration,
            QStringLiteral("The RTMP URL must end with a valid device identifier.")
        );
    }
    if (bindings_.containsDeviceId(deviceId)) {
        return fail(
            UserFailureReason::DuplicateDevice,
            QStringLiteral("Duplicate device identifier in RTMP URL.")
        );
    }
    if (bindings_.containsNameOrUrl(normalizedName, normalizedUrl)) {
        return fail(
            UserFailureReason::DuplicateDevice,
            QStringLiteral("Duplicate device name or connection URL.")
        );
    }

    const StreamId streamId =
        playbackManager_->addStream(normalizedName, normalizedUrl);
    if (streamId == kInvalidStreamId) {
        return fail(
            UserFailureReason::InternalFailure,
            QStringLiteral("Playback manager rejected the device.")
        );
    }

    VideoWidget *videoWidget =
        mainWindow_->addConnectionWidget(normalizedName);
    if (videoWidget == nullptr) {
        playbackManager_->removeStream(streamId);
        return fail(
            UserFailureReason::InternalFailure,
            QStringLiteral("Unable to create the device view.")
        );
    }

    bindings_.add({
        streamId,
        normalizedName,
        normalizedUrl,
        videoWidget,
        UserFailureReason::None,
        false,
        deviceId,
        {}
    });
    mainWindow_->bindVideoStream(
        videoWidget,
        streamId,
        playbackManager_->frameMailbox(streamId)
    );
    connectVideoWidget(*bindings_.find(streamId));
    publishStreamEventObservation(*bindings_.find(streamId));
    emit deviceBound(deviceId);
    if (selectedControlStreamId_ == kInvalidStreamId) {
        selectControlTarget(streamId);
    }
    eventReporter_->logSystem(
        LogLevel::Info,
        QStringLiteral("device"),
        QStringLiteral("connection_added"),
        QStringLiteral("Device connection added."),
        {},
        eventReporter_->context(*bindings_.find(streamId))
    );
    if (userInitiated) {
        eventReporter_->publish(
            UserEventType::DeviceAdded,
            UserFailureReason::None,
            bindings_.find(streamId)
        );
        eventReporter_->audit(
            AuditAction::AddDevice,
            AuditResult::Success,
            bindings_.find(streamId),
            normalizedName
        );
    }
    if (startImmediately) {
        playbackManager_->startStream(streamId);
    }
    return streamId;
}

StreamId StreamConnectionController::addConnection(
    const CameraStreamProfile &profile,
    const MediaServerEndpoint &endpoint,
    bool startImmediately
)
{
    const QString cameraId = profile.cameraId.trimmed();
    const auto reject =
        [this, &cameraId](const QString &technicalReason) {
            eventReporter_->logSystem(
                LogLevel::Warning,
                QStringLiteral("device"),
                QStringLiteral("connection_add_failed"),
                technicalReason,
                {{QStringLiteral("cameraId"), cameraId}}
            );
            return kInvalidStreamId;
        };

    // cameraId 在会话内唯一；重复 profile 不得覆盖既有绑定。
    if (!cameraId.isEmpty()) {
        if (bindings_.containsCameraId(cameraId)) {
            return reject(QStringLiteral(
                "Duplicate camera profile identifier."
            ));
        }
    }

    QString buildError;
    const std::optional<QUrl> generatedUrl =
        buildRtmpUrl(endpoint, profile.streamKey, &buildError);
    if (!generatedUrl.has_value()) {
        return reject(buildError);
    }

    const StreamId streamId = addConnection(
        profile.displayName,
        generatedUrl->toString(),
        startImmediately,
        false
    );
    if (streamId != kInvalidStreamId && !cameraId.isEmpty()) {
        ConnectionBinding *binding = bindings_.find(streamId);
        if (binding != nullptr) {
            binding->cameraId = cameraId;
            publishStreamEventObservation(*binding);
        }
    }
    return streamId;
}

bool StreamConnectionController::removeConnection(
    StreamId streamId,
    bool askForConfirmation
)
{
    ConnectionBinding *binding = bindings_.find(streamId);
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
            eventReporter_->audit(
                AuditAction::RemoveDevice,
                AuditResult::Cancelled,
                binding,
                binding->displayName,
                QStringLiteral("User cancelled the operation.")
            );
            return false;
        }
    }

    const ConnectionBinding removedBinding = *binding;
    VideoWidget *videoWidget = binding->videoWidget;
    binding->removing = true;
    if (!playbackManager_->removeStream(streamId)) {
        binding->removing = false;
        eventReporter_->publish(
            UserEventType::DeviceRemoveFailed,
            UserFailureReason::InternalFailure,
            binding
        );
        eventReporter_->audit(
            AuditAction::RemoveDevice,
            AuditResult::Failure,
            binding,
            binding->displayName,
            QStringLiteral("Playback manager failed to remove the device.")
        );
        return false;
    }
    if (!mainWindow_->removeConnectionWidget(videoWidget)) {
        eventReporter_->publish(
            UserEventType::DeviceRemoveFailed,
            UserFailureReason::InternalFailure,
            &removedBinding
        );
        eventReporter_->audit(
            AuditAction::RemoveDevice,
            AuditResult::Failure,
            &removedBinding,
            removedBinding.displayName,
            QStringLiteral("Device view removal failed.")
        );
        return false;
    }
    if (selectedControlStreamId_ == streamId) {
        selectedControlStreamId_ = kInvalidStreamId;
        if (removedBinding.videoWidget != nullptr) {
            removedBinding.videoWidget->setControlTargetSelected(false);
        }
        emit controlTargetChanged(kInvalidStreamId, {}, {});
    }
    emit streamRemovedObserved(streamEventObservation(removedBinding));
    bindings_.remove(streamId);
    emit deviceUnbound(removedBinding.deviceId);
    emit connectionRemoved(streamId, removedBinding.url);
    eventReporter_->logSystem(
        LogLevel::Info,
        QStringLiteral("device"),
        QStringLiteral("connection_removed"),
        QStringLiteral("Device connection removed."),
        {},
        eventReporter_->context(removedBinding)
    );
    eventReporter_->publish(
        UserEventType::DeviceRemoved,
        UserFailureReason::None,
        &removedBinding
    );
    eventReporter_->audit(
        AuditAction::RemoveDevice,
        AuditResult::Success,
        &removedBinding,
        removedBinding.displayName
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
    return bindings_.streamIdFor(videoWidget);
}

StreamId StreamConnectionController::selectedControlStreamId() const noexcept
{
    return selectedControlStreamId_;
}

ControlMediaObservation StreamConnectionController::controlMediaObservation(
    StreamId streamId
) const
{
    const ConnectionBinding *binding = bindings_.find(streamId);
    if (binding == nullptr) {
        return {};
    }
    const auto mailbox = playbackManager_->frameMailbox(streamId);
    return {
        binding->playbackStatus == DeviceStatus::Playing,
        mailbox != nullptr ? mailbox->lastPresentedFrameAgeMs() : -1,
    };
}

QString StreamConnectionController::deviceIdFromRtmpUrl(
    const QString &streamUrl)
{
    const std::optional<QString> id = DeviceIdentity::fromRtmpUrl(streamUrl);
    return id.value_or(QString());
}

QString StreamConnectionController::stableEventResourceId(
    const QString &cameraId,
    const QString &deviceId,
    const QString &streamUrl)
{
    const QString normalizedCamera = cameraId.trimmed();
    if (!normalizedCamera.isEmpty())
        return QStringLiteral("camera:") + normalizedCamera;
    const QString normalizedDevice = deviceId.trimmed();
    if (!normalizedDevice.isEmpty())
        return QStringLiteral("device-stream:") + normalizedDevice;

    QUrl url(streamUrl.trimmed());
    url.setUserInfo({});
    url.setQuery(QString());
    url.setFragment({});
    url.setScheme(url.scheme().toLower());
    url.setHost(url.host().toLower());
    QString path = QDir::cleanPath(url.path());
    if (!path.startsWith(QLatin1Char('/'))) path.prepend(QLatin1Char('/'));
    url.setPath(path);
    const QByteArray digest = QCryptographicHash::hash(
        url.toString(QUrl::FullyEncoded).toUtf8(),
        QCryptographicHash::Sha256).toHex();
    return QStringLiteral("stream-url-sha256:") +
           QString::fromLatin1(digest);
}

StreamEventObservation StreamConnectionController::streamEventObservation(
    const ConnectionBinding &binding) const
{
    return {
        binding.streamId,
        stableEventResourceId(binding.cameraId, binding.deviceId, binding.url),
        binding.deviceId,
        binding.displayName,
        binding.playbackStatus,
        binding.removing,
    };
}

void StreamConnectionController::publishStreamEventObservation(
    const ConnectionBinding &binding)
{
    emit streamEventObserved(streamEventObservation(binding));
}

void StreamConnectionController::setDevicePresence(
    const QString &deviceId, DevicePresenceState state)
{
    ConnectionBinding *binding = bindings_.findDeviceId(deviceId);
    if (binding == nullptr || binding->videoWidget == nullptr) return;
    binding->videoWidget->setDevicePresenceState(state);
}

void StreamConnectionController::setMediaServerEndpoint(
    const MediaServerEndpoint &endpoint
)
{
    mediaServerEndpoint_ = endpoint;
    hasMediaServerEndpoint_ = true;
}

void StreamConnectionController::showConnectionDialog()
{
    if (bindings_.isFull()) {
        return;
    }

    const int cameraNumber = bindings_.nextAvailableCameraNumber();
    const QString displayName = QStringLiteral("Camera %1")
                                    .arg(
                                        cameraNumber,
                                        2,
                                        10,
                                        QLatin1Char('0')
                                    );
    // 默认流名沿用既有 cameraNNN 命名；配置接入点后由 URL builder 生成
    // 默认 URL，生成失败时回退本机默认值，不影响手工完整 URL 输入。
    QString url;
    if (hasMediaServerEndpoint_) {
        const QString streamKey = QStringLiteral("camera%1")
                                      .arg(
                                          cameraNumber,
                                          3,
                                          10,
                                          QLatin1Char('0')
                                      );
        QString buildError;
        const std::optional<QUrl> generatedUrl =
            buildRtmpUrl(mediaServerEndpoint_, streamKey, &buildError);
        if (generatedUrl.has_value()) {
            url = generatedUrl->toString();
        } else {
            eventReporter_->logSystem(
                LogLevel::Warning,
                QStringLiteral("server"),
                QStringLiteral("default_url_build_failed"),
                buildError
            );
        }
    }
    if (url.isEmpty()) {
        url = QStringLiteral(
            "rtmp://127.0.0.1:1935/live/camera%1"
        ).arg(cameraNumber, 3, 10, QLatin1Char('0'));
    }

    ConnectionDialog dialog(mainWindow_);
    dialog.setExistingConnections(bindings_.names(), bindings_.urls());
    dialog.setDefaults(displayName, url);
    if (dialog.exec() == QDialog::Accepted) {
        addConnection(
            dialog.displayName(),
            dialog.streamUrl(),
            true,
            true
        );
    }
}

void StreamConnectionController::connectVideoWidget(ConnectionBinding &binding)
{
    VideoWidget *videoWidget = binding.videoWidget;
    const StreamId streamId = binding.streamId;
    connect(
        videoWidget, &VideoWidget::reconnectRequested,
        this,
        [this, streamId](VideoWidget *) {
            ConnectionBinding *binding = bindings_.find(streamId);
            const bool restarted =
                playbackManager_->restartStream(streamId);
            if (binding != nullptr) {
                eventReporter_->logSystem(
                    restarted ? LogLevel::Info : LogLevel::Warning,
                    QStringLiteral("device"),
                    QStringLiteral("manual_reconnect"),
                    restarted
                        ? QStringLiteral("Manual reconnect started.")
                        : QStringLiteral("Manual reconnect failed to start."),
                    {},
                    eventReporter_->context(*binding)
                );
            }
            if (binding != nullptr) {
                eventReporter_->publish(
                    restarted
                        ? UserEventType::ManualReconnectStarted
                        : UserEventType::OperationIncomplete,
                    restarted
                        ? UserFailureReason::None
                        : UserFailureReason::InternalFailure,
                        binding
                );
                eventReporter_->audit(
                    AuditAction::ManualReconnect,
                    restarted
                        ? AuditResult::Success
                        : AuditResult::Failure,
                    binding,
                    binding->displayName,
                    restarted
                        ? QString()
                        : QStringLiteral(
                              "Playback manager rejected the restart."
                          )
                );
            }
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
        videoWidget,
        &VideoWidget::audioToggleRequested,
        this,
        &StreamConnectionController::toggleAudio
    );
    connect(videoWidget, &VideoWidget::controlTargetRequested, this,
            [this, streamId](VideoWidget *) { selectControlTarget(streamId); });
}

void StreamConnectionController::selectControlTarget(StreamId streamId)
{
    ConnectionBinding *next = bindings_.find(streamId);
    if (next == nullptr || next->videoWidget == nullptr ||
        selectedControlStreamId_ == streamId) return;
    if (ConnectionBinding *previous = bindings_.find(selectedControlStreamId_);
        previous != nullptr && previous->videoWidget != nullptr) {
        previous->videoWidget->setControlTargetSelected(false);
    }
    selectedControlStreamId_ = streamId;
    next->videoWidget->setControlTargetSelected(true);
    emit controlTargetChanged(streamId, next->deviceId, next->url);
    emit controlTargetMediaChanged(streamId);
}

void StreamConnectionController::toggleAudio(VideoWidget *videoWidget)
{
    const StreamId streamId = bindings_.streamIdFor(videoWidget);
    if (streamId == kInvalidStreamId) return;

    if (playbackManager_->selectedAudioStream() != streamId) {
        if (playbackManager_->selectAudioStream(streamId)) {
            playbackManager_->setAudioMuted(false);
        }
    } else {
        playbackManager_->setAudioMuted(!playbackManager_->isAudioMuted());
    }
}
