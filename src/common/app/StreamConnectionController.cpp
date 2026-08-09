#include "app/StreamConnectionController.h"

#include <QMessageBox>
#include <QSet>

#include <algorithm>
#include <optional>

#include "logging/LogManager.h"
#include "logging/UserMessageService.h"
#include "media/FFmpegPlayer.h"
#include "media/MultiStreamPlaybackManager.h"
#include "server/RtmpUrlBuilder.h"
#include "ui/ConnectionDialog.h"
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

namespace {

QString currentAuditActor()
{
    QString actor = qEnvironmentVariable("USERNAME").trimmed();
    if (actor.isEmpty()) {
        actor = qEnvironmentVariable("USER").trimmed();
    }
    return actor.isEmpty() ? QStringLiteral("local-user") : actor;
}

} // namespace

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
    , logManager_(logManager)
    , userMessageService_(userMessageService)
{
    Q_ASSERT(mainWindow_ != nullptr);
    Q_ASSERT(playbackManager_ != nullptr);

    connect(
        mainWindow_, &MainWindow::addConnectionRequested,
        this, &StreamConnectionController::showConnectionDialog
    );
    connect(
        playbackManager_, &MultiStreamPlaybackManager::stateChanged,
        this,
        [this](StreamId streamId, DeviceStatus state) {
            Binding *binding = bindingFor(streamId);
            if (binding != nullptr) {
                mainWindow_->updateDeviceStatus(
                    binding->videoWidget,
                    state,
                    binding->lastFailureReason
                );
                logDeviceState(*binding, state);
                if (state == DeviceStatus::Playing) {
                    binding->lastFailureReason =
                        UserFailureReason::None;
                    publishUserEvent(
                        UserEventType::DeviceConnected,
                        UserFailureReason::None,
                        binding
                    );
                } else if (
                    state == DeviceStatus::Disconnected &&
                    !binding->removing
                ) {
                    publishUserEvent(
                        UserEventType::DeviceDisconnected,
                        UserFailureReason::None,
                        binding
                    );
                }
            }
        }
    );
    connect(
        playbackManager_, &MultiStreamPlaybackManager::errorOccurred,
        this,
        [this](StreamId streamId, const PlaybackError &error) {
            Binding *binding = bindingFor(streamId);
            if (binding != nullptr) {
                const UserFailureReason mappedReason =
                    userReason(error.code);
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
                if (logManager_ != nullptr) {
                    logManager_->logSystem(
                        LogLevel::Warning,
                        QStringLiteral("media"),
                        QStringLiteral("stream_error"),
                        error.technicalMessage,
                        {
                            {
                                QStringLiteral("errorCode"),
                                static_cast<int>(error.code)
                            },
                            {
                                QStringLiteral("nativeErrorCode"),
                                error.nativeCode
                            },
                            {
                                QStringLiteral("recoverable"),
                                error.recoverable
                            }
                        },
                        logContext(*binding),
                        true
                    );
                }
                publishUserEvent(
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
            Binding *binding = bindingFor(streamId);
            if (binding == nullptr) {
                return;
            }
            mainWindow_->updateDeviceStatus(
                binding->videoWidget,
                DeviceStatus::Reconnecting
            );
            if (logManager_ != nullptr) {
                logManager_->logSystem(
                    LogLevel::Info,
                    QStringLiteral("media"),
                    QStringLiteral("reconnect_scheduled"),
                    QStringLiteral("Automatic reconnect scheduled."),
                    {
                        {QStringLiteral("delayMs"), delayMs},
                        {
                            QStringLiteral("consecutiveFailures"),
                            consecutiveFailures
                        }
                    },
                    logContext(*binding),
                    true
                );
            }
        }
    );
}

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
            if (logManager_ != nullptr) {
                logManager_->logSystem(
                    LogLevel::Warning,
                    QStringLiteral("device"),
                    QStringLiteral("connection_add_failed"),
                    technicalReason,
                    {
                        {
                            QStringLiteral("failureReason"),
                            static_cast<int>(reason)
                        }
                    },
                    {0, normalizedName, normalizedUrl}
                );
            }
            if (userInitiated) {
                publishUserEvent(
                    UserEventType::DeviceAddFailed,
                    reason,
                    nullptr,
                    normalizedName
                );
                writeAudit(
                    AuditAction::AddDevice,
                    AuditResult::Failure,
                    nullptr,
                    normalizedName,
                    technicalReason
                );
            }
            return kInvalidStreamId;
        };

    if (bindings_.size() >= 16) {
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
    const bool duplicate = std::any_of(
        bindings_.begin(), bindings_.end(),
        [&](const Binding &binding) {
            return binding.displayName == normalizedName ||
                   binding.url == normalizedUrl;
        }
    );
    if (duplicate) {
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

    bindings_.push_back({
        streamId,
        normalizedName,
        normalizedUrl,
        videoWidget,
        UserFailureReason::None,
        false,
        {}
    });
    mainWindow_->bindVideoStream(
        videoWidget,
        streamId,
        playbackManager_->frameMailbox(streamId)
    );
    connectVideoWidget(bindings_.back());
    if (logManager_ != nullptr) {
        logManager_->logSystem(
            LogLevel::Info,
            QStringLiteral("device"),
            QStringLiteral("connection_added"),
            QStringLiteral("Device connection added."),
            {},
            logContext(bindings_.back())
        );
    }
    if (userInitiated) {
        publishUserEvent(
            UserEventType::DeviceAdded,
            UserFailureReason::None,
            &bindings_.back()
        );
        writeAudit(
            AuditAction::AddDevice,
            AuditResult::Success,
            &bindings_.back(),
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
            if (logManager_ != nullptr) {
                logManager_->logSystem(
                    LogLevel::Warning,
                    QStringLiteral("device"),
                    QStringLiteral("connection_add_failed"),
                    technicalReason,
                    {{QStringLiteral("cameraId"), cameraId}}
                );
            }
            return kInvalidStreamId;
        };

    // cameraId 在会话内唯一；重复 profile 不得覆盖既有绑定。
    if (!cameraId.isEmpty()) {
        const bool duplicateCameraId = std::any_of(
            bindings_.begin(), bindings_.end(),
            [&cameraId](const Binding &binding) {
                return !binding.cameraId.isEmpty() &&
                       binding.cameraId == cameraId;
            }
        );
        if (duplicateCameraId) {
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
        Binding *binding = bindingFor(streamId);
        if (binding != nullptr) {
            binding->cameraId = cameraId;
        }
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
            writeAudit(
                AuditAction::RemoveDevice,
                AuditResult::Cancelled,
                binding,
                binding->displayName,
                QStringLiteral("User cancelled the operation.")
            );
            return false;
        }
    }

    const Binding removedBinding = *binding;
    VideoWidget *videoWidget = binding->videoWidget;
    binding->removing = true;
    if (!playbackManager_->removeStream(streamId)) {
        binding->removing = false;
        publishUserEvent(
            UserEventType::DeviceRemoveFailed,
            UserFailureReason::InternalFailure,
            binding
        );
        writeAudit(
            AuditAction::RemoveDevice,
            AuditResult::Failure,
            binding,
            binding->displayName,
            QStringLiteral("Playback manager failed to remove the device.")
        );
        return false;
    }
    if (!mainWindow_->removeConnectionWidget(videoWidget)) {
        publishUserEvent(
            UserEventType::DeviceRemoveFailed,
            UserFailureReason::InternalFailure,
            &removedBinding
        );
        writeAudit(
            AuditAction::RemoveDevice,
            AuditResult::Failure,
            &removedBinding,
            removedBinding.displayName,
            QStringLiteral("Device view removal failed.")
        );
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
    if (logManager_ != nullptr) {
        logManager_->logSystem(
            LogLevel::Info,
            QStringLiteral("device"),
            QStringLiteral("connection_removed"),
            QStringLiteral("Device connection removed."),
            {},
            logContext(removedBinding)
        );
    }
    publishUserEvent(
        UserEventType::DeviceRemoved,
        UserFailureReason::None,
        &removedBinding
    );
    writeAudit(
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

void StreamConnectionController::setMediaServerEndpoint(
    const MediaServerEndpoint &endpoint
)
{
    mediaServerEndpoint_ = endpoint;
    hasMediaServerEndpoint_ = true;
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
        } else if (logManager_ != nullptr) {
            logManager_->logSystem(
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
        addConnection(
            dialog.displayName(),
            dialog.streamUrl(),
            true,
            true
        );
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
            Binding *binding = bindingFor(streamId);
            const bool restarted =
                playbackManager_->restartStream(streamId);
            if (binding != nullptr && logManager_ != nullptr) {
                logManager_->logSystem(
                    restarted ? LogLevel::Info : LogLevel::Warning,
                    QStringLiteral("device"),
                    QStringLiteral("manual_reconnect"),
                    restarted
                        ? QStringLiteral("Manual reconnect started.")
                        : QStringLiteral("Manual reconnect failed to start."),
                    {},
                    logContext(*binding)
                );
            }
            if (binding != nullptr) {
                publishUserEvent(
                    restarted
                        ? UserEventType::ManualReconnectStarted
                        : UserEventType::OperationIncomplete,
                    restarted
                        ? UserFailureReason::None
                        : UserFailureReason::InternalFailure,
                    binding
                );
                writeAudit(
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
}

LogContext StreamConnectionController::logContext(
    const Binding &binding
) const
{
    return {binding.streamId, binding.displayName, binding.url};
}

void StreamConnectionController::logDeviceState(
    const Binding &binding,
    DeviceStatus status
)
{
    if (logManager_ == nullptr) {
        return;
    }
    QString state;
    LogLevel level = LogLevel::Info;
    bool aggregate = false;
    switch (status) {
    case DeviceStatus::Disconnected:
        state = QStringLiteral("disconnected");
        break;
    case DeviceStatus::Connecting:
        state = QStringLiteral("connecting");
        aggregate = true;
        break;
    case DeviceStatus::Playing:
        state = QStringLiteral("playing");
        break;
    case DeviceStatus::Reconnecting:
        state = QStringLiteral("reconnecting");
        aggregate = true;
        break;
    case DeviceStatus::Error:
        state = QStringLiteral("error");
        level = LogLevel::Warning;
        aggregate = true;
        break;
    }
    logManager_->logSystem(
        level,
        QStringLiteral("device"),
        QStringLiteral("status_changed"),
        QStringLiteral("Device status changed."),
        {{QStringLiteral("state"), state}},
        logContext(binding),
        aggregate
    );
}

void StreamConnectionController::publishUserEvent(
    UserEventType type,
    UserFailureReason reason,
    const Binding *binding,
    const QString &displayName
)
{
    if (userMessageService_ == nullptr) {
        return;
    }
    userMessageService_->publish({
        type,
        reason,
        binding != nullptr ? binding->streamId : 0,
        binding != nullptr ? binding->displayName : displayName
    });
}

void StreamConnectionController::writeAudit(
    AuditAction action,
    AuditResult result,
    const Binding *binding,
    const QString &displayName,
    const QString &reason
)
{
    if (logManager_ == nullptr) {
        return;
    }
    AuditRecord record;
    record.actor = currentAuditActor();
    record.action = action;
    record.targetType = QStringLiteral("Camera");
    record.targetId = binding != nullptr
        ? QString::number(binding->streamId)
        : displayName;
    record.result = result;
    record.reason = reason;
    record.source = QStringLiteral("local-ui");
    QJsonObject values;
    values.insert(
        QStringLiteral("deviceName"),
        binding != nullptr ? binding->displayName : displayName
    );
    if (binding != nullptr) {
        values.insert(QStringLiteral("connectionUrl"), binding->url);
    }
    if (action == AuditAction::RemoveDevice) {
        record.beforeValues = values;
    } else {
        record.afterValues = values;
    }
    logManager_->logAudit(record);
}

UserFailureReason StreamConnectionController::userReason(
    PlaybackErrorCode code
)
{
    switch (code) {
    case PlaybackErrorCode::ConnectionTimeout:
        return UserFailureReason::ConnectionTimeout;
    case PlaybackErrorCode::HostUnavailable:
        return UserFailureReason::HostUnavailable;
    case PlaybackErrorCode::AuthenticationFailed:
        return UserFailureReason::AuthenticationFailed;
    case PlaybackErrorCode::InvalidConfiguration:
        return UserFailureReason::InvalidConfiguration;
    case PlaybackErrorCode::MediaUnavailable:
    case PlaybackErrorCode::UnsupportedMedia:
    case PlaybackErrorCode::DecodeFailure:
        return UserFailureReason::MediaUnavailable;
    case PlaybackErrorCode::AlreadyRunning:
    case PlaybackErrorCode::RuntimeInitializationFailed:
    case PlaybackErrorCode::ResourceFailure:
    case PlaybackErrorCode::RetryLimitReached:
    case PlaybackErrorCode::Unknown:
        return UserFailureReason::Unknown;
    }
    return UserFailureReason::Unknown;
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
