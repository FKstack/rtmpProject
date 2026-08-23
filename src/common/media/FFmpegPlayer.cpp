#include "media/FFmpegPlayer.h"

#include "EncodedVideoDecodeSessionFfmpegAccess.h"
#include "FfmpegInputSession.h"
#include "FfmpegSessionTypes.h"
#include "media/EncodedVideoDecodeSession.h"

#include <QMetaObject>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {

int defaultDecodeWorkerCount()
{
    const int ideal = QThread::idealThreadCount();
    return std::clamp(ideal > 0 ? ideal / 2 : 1, 1, 8);
}

PlaybackPerformanceOptions singleWorkerOptions()
{
    PlaybackPerformanceOptions options;
    options.decodeWorkerCount = 1;
    return options;
}

} // namespace

FFmpegPlayer::FFmpegPlayer(QObject *parent)
    : FFmpegPlayer(
          1,
          QStringLiteral("Camera 01"),
          nullptr,
          singleWorkerOptions(),
          parent
      )
{
}

FFmpegPlayer::FFmpegPlayer(
    StreamId streamId,
    QString displayName,
    DecodeWorkerPool *decodeWorkerPool,
    PlaybackPerformanceOptions options,
    QObject *parent
)
    : QObject(parent)
    , decodeWorkerPool_(decodeWorkerPool)
    , streamId_(streamId)
    , displayName_(std::move(displayName))
    , options_(options)
{
    decodeSession_ = std::make_unique<EncodedVideoDecodeSession>(
        streamId_,
        displayName_,
        decodeWorkerPool_,
        options_,
        [this](DeviceStatus state, std::uint64_t generation) {
            postState(state, generation);
        },
        [this](PlaybackError error, std::uint64_t generation) {
            postError(std::move(error), generation);
            if (generation ==
                sessionId_.load(std::memory_order_acquire)) {
                restartRequested_.store(true, std::memory_order_release);
                reconnectCondition_.notify_all();
            }
        }
    );
    qRegisterMetaType<DeviceStatus>();
    qRegisterMetaType<PlaybackError>();
    (void)FfmpegInputSession::networkRuntimeAvailable();
}

FFmpegPlayer::~FFmpegPlayer()
{
    stop();
}

void FFmpegPlayer::setAudioPacketSink(AudioPacketSink *sink) noexcept
{
    Q_ASSERT(!isRunning());
    audioPacketSink_ = sink;
}

bool FFmpegPlayer::start(const QString &rtmpUrl)
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (isRunning()) {
        emit errorOccurred(
            {PlaybackErrorCode::AlreadyRunning, 0,
             tr("播放器已经在运行。"), false}
        );
        return false;
    }
    if (!FfmpegInputSession::networkRuntimeAvailable()) {
        emit errorOccurred(
            {PlaybackErrorCode::RuntimeInitializationFailed, 0,
             tr("FFmpeg 网络模块初始化失败。"), false}
        );
        setStateOnOwnerThread(DeviceStatus::Error);
        return false;
    }

    const QUrl parsedUrl(rtmpUrl, QUrl::StrictMode);
    if (!parsedUrl.isValid() ||
        parsedUrl.scheme().compare(
            QStringLiteral("rtmp"), Qt::CaseInsensitive
        ) != 0 ||
        parsedUrl.host().isEmpty() || parsedUrl.path().isEmpty()) {
        emit errorOccurred(
            {PlaybackErrorCode::InvalidConfiguration, 0,
             tr("RTMP URL 无效；仅支持 rtmp:// 地址。"), false}
        );
        setStateOnOwnerThread(DeviceStatus::Error);
        return false;
    }

    if (decodeWorkerPool_ == nullptr) {
        ownedDecodeWorkerPool_ = std::make_unique<DecodeWorkerPool>(
            options_.decodeWorkerCount > 0
                ? options_.decodeWorkerCount
                : defaultDecodeWorkerCount()
        );
        decodeWorkerPool_ = ownedDecodeWorkerPool_.get();
        EncodedVideoDecodeSessionFfmpegAccess::attachPool(
            *decodeSession_, decodeWorkerPool_
        );
    }

    if (networkThread_ != nullptr) {
        networkThread_->wait();
        networkThread_.reset();
    }

    stopRequested_.store(false, std::memory_order_release);
    restartRequested_.store(false, std::memory_order_release);
    const std::uint64_t newSessionId =
        sessionId_.fetch_add(1, std::memory_order_acq_rel) + 1;
    EncodedVideoDecodeSessionFfmpegAccess::prepare(
        *decodeSession_, newSessionId
    );

    networkThread_.reset(QThread::create(
        [this, rtmpUrl, newSessionId] {
            decodeNetworkLoop(rtmpUrl, newSessionId);
        }
    ));
    networkThread_->setObjectName(
        objectName().isEmpty()
            ? QStringLiteral("FFmpegNetworkThread")
            : objectName() + QStringLiteral("NetworkThread")
    );
    networkThread_->start();
    return true;
}

void FFmpegPlayer::requestStop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    stopRequested_.store(true, std::memory_order_release);
    restartRequested_.store(false, std::memory_order_release);
    reconnectCondition_.notify_all();
}

void FFmpegPlayer::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (networkThread_ == nullptr &&
        state_ == DeviceStatus::Disconnected &&
        decodeSession_->activeGeneration() == 0) {
        return;
    }
    requestStop();

    if (networkThread_ != nullptr) {
        networkThread_->wait();
        networkThread_.reset();
    }

    // Advancing the generation before decoder shutdown makes queued owner
    // callbacks stale before any resource is released.
    const std::uint64_t oldSession =
        sessionId_.fetch_add(1, std::memory_order_acq_rel);
    decodeSession_->closeGeneration(oldSession);
    if (audioPacketSink_ != nullptr) {
        audioPacketSink_->invalidateAudioSession(streamId_, oldSession);
    }
    setStateOnOwnerThread(DeviceStatus::Disconnected);
}

bool FFmpegPlayer::isRunning() const noexcept
{
    Q_ASSERT(QThread::currentThread() == thread());
    return networkThread_ != nullptr && networkThread_->isRunning();
}

std::shared_ptr<LatestFrameMailbox> FFmpegPlayer::frameMailbox() const
{
    return decodeSession_->frameMailbox();
}

StreamMetrics FFmpegPlayer::metricsSnapshot()
{
    Q_ASSERT(QThread::currentThread() == thread());
    return decodeSession_->metricsSnapshot(state_);
}

void FFmpegPlayer::decodeNetworkLoop(
    QString rtmpUrl,
    std::uint64_t sessionId
)
{
    const int reconnectDelayMs = std::max(1, options_.reconnectDelayMs);
    const int maximumFailures =
        std::max(0, options_.maximumConsecutiveFailures);
    int consecutiveFailures = 0;
    bool failureLimitReached = false;

    while (!stopRequested_.load(std::memory_order_acquire)) {
        postState(DeviceStatus::Connecting, sessionId);
        restartRequested_.store(false, std::memory_order_release);
        if (audioPacketSink_ != nullptr) {
            audioPacketSink_->invalidateAudioSession(streamId_, sessionId);
        }

        FfmpegInputSession inputSession(
            stopRequested_,
            restartRequested_,
            [this](const std::shared_ptr<void> &configuration,
                   std::uint64_t generation) {
                const auto typed =
                    std::static_pointer_cast<FfmpegCodecConfiguration>(
                        configuration
                    );
                if (typed->kind == FfmpegTrackKind::Audio) {
                    enqueueAudioConfiguration(configuration, generation);
                } else {
                    enqueueDecoderConfiguration(configuration, generation);
                }
            },
            [this](FfmpegTrackKind kind, void *packet, qint64 receivedAt,
                   std::uint64_t generation) {
                if (kind == FfmpegTrackKind::Audio) {
                    enqueueAudioPacket(packet, receivedAt, generation);
                } else {
                    enqueuePacket(packet, receivedAt, generation);
                }
            }
        );
        const FfmpegInputResult inputResult =
            inputSession.run(rtmpUrl, sessionId);

        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }

        if (inputResult.receivedPackets) {
            consecutiveFailures = 0;
        }
        ++consecutiveFailures;
        postState(DeviceStatus::Error, sessionId);
        if (!inputResult.message.isEmpty()) {
            postError(
                {
                    inputResult.errorCode,
                    inputResult.nativeCode,
                    inputResult.message,
                    true
                },
                sessionId
            );
        }

        if (maximumFailures > 0 &&
            consecutiveFailures >= maximumFailures) {
            failureLimitReached = true;
            postError(
                {
                    PlaybackErrorCode::RetryLimitReached,
                    0,
                    tr("已达到连续失败上限（%1 次），自动重连已停止。")
                        .arg(maximumFailures),
                    false
                },
                sessionId
            );
            break;
        }

        decodeSession_->recordReconnect();
        postState(DeviceStatus::Reconnecting, sessionId);
        postReconnectScheduled(
            consecutiveFailures, reconnectDelayMs, sessionId
        );
        if (!waitForReconnect(reconnectDelayMs)) {
            break;
        }
    }

    if (!failureLimitReached) {
        postState(DeviceStatus::Disconnected, sessionId);
    }
}

void FFmpegPlayer::enqueueAudioConfiguration(
    const std::shared_ptr<void> &codecConfiguration,
    std::uint64_t sessionId
)
{
    if (audioPacketSink_ != nullptr) {
        audioPacketSink_->submitAudioConfiguration(
            streamId_, codecConfiguration, sessionId
        );
    }
}

void FFmpegPlayer::enqueueAudioPacket(
    void *packet,
    qint64 receivedMonotonicMs,
    std::uint64_t sessionId
)
{
    if (audioPacketSink_ != nullptr) {
        audioPacketSink_->submitAudioPacket(
            streamId_, packet, receivedMonotonicMs, sessionId
        );
        return;
    }
    AVPacket *owned = static_cast<AVPacket *>(packet);
    av_packet_free(&owned);
}

void FFmpegPlayer::enqueueDecoderConfiguration(
    const std::shared_ptr<void> &codecConfiguration,
    std::uint64_t sessionId
)
{
    EncodedVideoDecodeSessionFfmpegAccess::configure(
        *decodeSession_, codecConfiguration, sessionId
    );
}

void FFmpegPlayer::enqueuePacket(
    void *packet,
    qint64 receivedMonotonicMs,
    std::uint64_t sessionId
)
{
    (void)EncodedVideoDecodeSessionFfmpegAccess::submitPacket(
        *decodeSession_, packet, receivedMonotonicMs, sessionId
    );
}

void FFmpegPlayer::postState(
    DeviceStatus state,
    std::uint64_t sessionId
)
{
    QMetaObject::invokeMethod(
        this,
        [this, state, sessionId] {
            if (sessionId ==
                sessionId_.load(std::memory_order_acquire)) {
                setStateOnOwnerThread(state);
            }
        },
        Qt::QueuedConnection
    );
}

void FFmpegPlayer::postReconnectScheduled(
    int consecutiveFailures,
    int delayMs,
    std::uint64_t sessionId
)
{
    QMetaObject::invokeMethod(
        this,
        [this, consecutiveFailures, delayMs, sessionId] {
            if (sessionId ==
                sessionId_.load(std::memory_order_acquire)) {
                emit reconnectScheduled(consecutiveFailures, delayMs);
            }
        },
        Qt::QueuedConnection
    );
}

void FFmpegPlayer::postError(
    PlaybackError error,
    std::uint64_t sessionId
)
{
    QMetaObject::invokeMethod(
        this,
        [this, error = std::move(error), sessionId] {
            if (sessionId ==
                sessionId_.load(std::memory_order_acquire)) {
                emit errorOccurred(error);
            }
        },
        Qt::QueuedConnection
    );
}

void FFmpegPlayer::setStateOnOwnerThread(DeviceStatus state)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

bool FFmpegPlayer::waitForReconnect(int delayMs)
{
    std::unique_lock<std::mutex> lock(reconnectMutex_);
    const bool interrupted = reconnectCondition_.wait_for(
        lock,
        std::chrono::milliseconds(delayMs),
        [this] {
            return stopRequested_.load(std::memory_order_acquire) ||
                   restartRequested_.load(std::memory_order_acquire);
        }
    );
    return !stopRequested_.load(std::memory_order_acquire) &&
           (!interrupted ||
            restartRequested_.load(std::memory_order_acquire));
}
