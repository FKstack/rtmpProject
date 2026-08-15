#include "media/FFmpegPlayer.h"
#include "FfmpegInputSession.h"
#include "FfmpegSessionTypes.h"
#include "FfmpegVideoFrameAdapter.h"
#include "media/LatencyMarkerCodec.h"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}
namespace {

constexpr int kDecodeBatchPackets = 4;
constexpr qint64 kDecodeBatchMilliseconds = 5;

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           )
        .count();
}

QString ffmpegError(int errorCode)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer {};
    if (av_strerror(errorCode, buffer.data(), buffer.size()) < 0) {
        return QStringLiteral("未知 FFmpeg 错误 (%1)").arg(errorCode);
    }
    return QString::fromUtf8(buffer.data());
}

struct EncodedPacket
{
    AVPacket *packet = nullptr;
    qint64 receivedMonotonicMs = 0;
    std::uint64_t sessionId = 0;

    ~EncodedPacket()
    {
        av_packet_free(&packet);
    }
};

struct DecoderRuntime
{
    AVCodecContext *codecContext = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *normalizationFrame = nullptr;
    SwsContext *normalizationContext = nullptr;
    std::uint64_t sessionId = 0;
    int diagnosticWidth = 0;
    int diagnosticHeight = 0;
    int diagnosticFormat = -1;
    bool metadataFallbackReported = false;
    bool unsupportedTransferReported = false;
    bool hasLastSourceSequence = false;
    std::uint32_t lastSourceSequence = 0;

    ~DecoderRuntime()
    {
        reset();
    }

    void reset()
    {
        if (normalizationContext != nullptr) {
            sws_freeContext(normalizationContext);
            normalizationContext = nullptr;
        }
        if (normalizationFrame != nullptr) {
            av_frame_free(&normalizationFrame);
        }
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (codecContext != nullptr) {
            avcodec_free_context(&codecContext);
        }
        sessionId = 0;
        diagnosticWidth = 0;
        diagnosticHeight = 0;
        diagnosticFormat = -1;
        metadataFallbackReported = false;
        unsupportedTransferReported = false;
        hasLastSourceSequence = false;
        lastSourceSequence = 0;
    }
};

AVFrame *normalizeForRendering(DecoderRuntime &decoder, AVFrame *source)
{
    if (source == nullptr) {
        return nullptr;
    }
    const auto sourceFormat = static_cast<AVPixelFormat>(source->format);
    if (sourceFormat == AV_PIX_FMT_YUV420P ||
        sourceFormat == AV_PIX_FMT_YUVJ420P ||
        sourceFormat == AV_PIX_FMT_NV12) {
        return source;
    }

    if (decoder.normalizationFrame == nullptr) {
        decoder.normalizationFrame = av_frame_alloc();
    }
    AVFrame *destination = decoder.normalizationFrame;
    if (destination == nullptr) {
        return nullptr;
    }
    av_frame_unref(destination);
    destination->format = AV_PIX_FMT_YUV420P;
    destination->width = source->width;
    destination->height = source->height;
    if (av_frame_get_buffer(destination, 32) < 0 ||
        av_frame_make_writable(destination) < 0) {
        av_frame_unref(destination);
        return nullptr;
    }

    decoder.normalizationContext = sws_getCachedContext(
        decoder.normalizationContext,
        source->width,
        source->height,
        sourceFormat,
        source->width,
        source->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );
    if (decoder.normalizationContext == nullptr ||
        sws_scale(
            decoder.normalizationContext,
            source->data,
            source->linesize,
            0,
            source->height,
            destination->data,
            destination->linesize
        ) <= 0) {
        av_frame_unref(destination);
        return nullptr;
    }
    if (av_frame_copy_props(destination, source) < 0) {
        av_frame_unref(destination);
        return nullptr;
    }
    return destination;
}

QString stateName(DeviceStatus state)
{
    switch (state) {
    case DeviceStatus::Disconnected:
        return QStringLiteral("disconnected");
    case DeviceStatus::Connecting:
        return QStringLiteral("connecting");
    case DeviceStatus::Playing:
        return QStringLiteral("playing");
    case DeviceStatus::Reconnecting:
        return QStringLiteral("reconnecting");
    case DeviceStatus::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

int defaultDecodeWorkerCount()
{
    const int ideal = QThread::idealThreadCount();
    return std::clamp(ideal > 0 ? ideal / 2 : 1, 1, 8);
}

} // namespace

/**
 * @brief Implementation-private decode session with bounded queue and worker affinity.
 *
 * Network input only submits configurations/packets. This session owns the
 * decoder runtime, latest-frame mailbox and decode-side counters.
 */
struct FFmpegPlayer::StreamDecodeSession
{
    FFmpegPlayer *owner = nullptr;
    DecodeWorkerPool *pool = nullptr;
    int workerIndex = 0;
    PlaybackPerformanceOptions options;
    StreamId streamId = kInvalidStreamId;

    std::mutex queueMutex;
    std::condition_variable idleCondition;
    std::deque<std::shared_ptr<EncodedPacket>> packets;
    qint64 queueBytes = 0;
    std::shared_ptr<FfmpegCodecConfiguration> codecConfiguration;
    std::uint64_t configurationSessionId = 0;
    bool decoderResetRequired = false;
    bool dropUntilKeyframe = false;
    bool decodeScheduled = false;
    bool stopping = false;
    DecoderRuntime decoder;

    std::shared_ptr<LatestFrameMailbox> frameMailbox =
        std::make_shared<LatestFrameMailbox>();
    std::atomic_uint64_t frameSequence {0};

    std::atomic_uint64_t packetsReceived {0};
    std::atomic_uint64_t packetBytesReceived {0};
    std::atomic_uint64_t packetsDropped {0};
    std::atomic_uint64_t decodedFrames {0};
    std::atomic_uint64_t convertedFrames {0};
    std::atomic_uint64_t unsupportedFrames {0};
    std::atomic_uint64_t markerDecodedFrames {0};
    std::atomic_uint64_t markerDecodeFailures {0};
    std::atomic_uint64_t sourceSequenceGaps {0};
    std::atomic_uint64_t reconnectCount {0};
    std::atomic_uint64_t playingSessionId {0};
    std::atomic<qint64> lastFrameMonotonicMs {-1};

};

FFmpegPlayer::FFmpegPlayer(QObject *parent)
    : QObject(parent)
{
    options_.decodeWorkerCount = 1;
    decodeSession_ = std::make_shared<StreamDecodeSession>();
    decodeSession_->owner = this;
    decodeSession_->workerIndex = 0;
    decodeSession_->options = options_;
    decodeSession_->streamId = streamId_;
    qRegisterMetaType<DeviceStatus>();
    qRegisterMetaType<PlaybackError>();
    (void)FfmpegInputSession::networkRuntimeAvailable();
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
    if (decodeWorkerPool_ == nullptr) {
        ownedDecodeWorkerPool_ = std::make_unique<DecodeWorkerPool>(
            options_.decodeWorkerCount > 0
                ? options_.decodeWorkerCount
                : defaultDecodeWorkerCount()
        );
        decodeWorkerPool_ = ownedDecodeWorkerPool_.get();
    }

    decodeSession_ = std::make_shared<StreamDecodeSession>();
    decodeSession_->owner = this;
    decodeSession_->pool = decodeWorkerPool_;
    decodeSession_->workerIndex = decodeWorkerPool_->workerIndexFor(streamId_);
    decodeSession_->options = options_;
    decodeSession_->streamId = streamId_;
    qRegisterMetaType<DeviceStatus>();
    qRegisterMetaType<PlaybackError>();
    (void)FfmpegInputSession::networkRuntimeAvailable();
}

FFmpegPlayer::~FFmpegPlayer()
{
    stop();
    decodeSession_->owner = nullptr;
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
        parsedUrl.scheme().compare(QStringLiteral("rtmp"), Qt::CaseInsensitive) != 0 ||
        parsedUrl.host().isEmpty() || parsedUrl.path().isEmpty()) {
        emit errorOccurred(
            {PlaybackErrorCode::InvalidConfiguration, 0,
             tr("RTMP URL 无效；仅支持 rtmp:// 地址。"), false}
        );
        setStateOnOwnerThread(DeviceStatus::Error);
        return false;
    }

    if (decodeWorkerPool_ == nullptr) {
        ownedDecodeWorkerPool_ = std::make_unique<DecodeWorkerPool>(1);
        decodeWorkerPool_ = ownedDecodeWorkerPool_.get();
        decodeSession_->pool = decodeWorkerPool_;
        decodeSession_->workerIndex = 0;
    }

    if (networkThread_ != nullptr) {
        networkThread_->wait();
        networkThread_.reset();
    }

    stopRequested_.store(false, std::memory_order_release);
    restartRequested_.store(false, std::memory_order_release);
    const std::uint64_t newSessionId =
        sessionId_.fetch_add(1, std::memory_order_acq_rel) + 1;

    {
        const std::lock_guard<std::mutex> lock(decodeSession_->queueMutex);
        decodeSession_->packets.clear();
        decodeSession_->queueBytes = 0;
        decodeSession_->codecConfiguration.reset();
        decodeSession_->configurationSessionId = 0;
        decodeSession_->decoderResetRequired = true;
        decodeSession_->dropUntilKeyframe = false;
        decodeSession_->stopping = false;
    }
    decodeSession_->frameMailbox->clear();

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
        state_ == DeviceStatus::Disconnected) {
        return;
    }
    requestStop();

    if (networkThread_ != nullptr) {
        networkThread_->wait();
        networkThread_.reset();
    }

    {
        std::unique_lock<std::mutex> lock(decodeSession_->queueMutex);
        decodeSession_->stopping = true;
        decodeSession_->packets.clear();
        decodeSession_->queueBytes = 0;
        decodeSession_->decoderResetRequired = true;
        if (decodeSession_->decodeScheduled) {
            decodeSession_->idleCondition.wait(lock, [this] {
                return !decodeSession_->decodeScheduled;
            });
        } else {
            // 没有 worker 正在访问解码上下文时可直接释放；不要为了空播放器
            // 额外投递清理任务，避免析构阶段创建无意义的异步工作。
            decodeSession_->decoder.reset();
        }
        decodeSession_->codecConfiguration.reset();
        decodeSession_->stopping = false;
    }

    const std::uint64_t oldSession =
        sessionId_.fetch_add(1, std::memory_order_acq_rel);
    if (audioPacketSink_ != nullptr) {
        audioPacketSink_->invalidateAudioSession(streamId_, oldSession);
    }
    decodeSession_->frameMailbox->clear();
    setStateOnOwnerThread(DeviceStatus::Disconnected);
}

bool FFmpegPlayer::isRunning() const noexcept
{
    Q_ASSERT(QThread::currentThread() == thread());
    return networkThread_ != nullptr && networkThread_->isRunning();
}

std::shared_ptr<LatestFrameMailbox> FFmpegPlayer::frameMailbox() const
{
    return decodeSession_->frameMailbox;
}

StreamMetrics FFmpegPlayer::metricsSnapshot()
{
    Q_ASSERT(QThread::currentThread() == thread());

    const qint64 now = monotonicMilliseconds();
    const std::uint64_t decoded =
        decodeSession_->decodedFrames.load(std::memory_order_relaxed);
    const LatestFrameMailboxStats mailboxStats = decodeSession_->frameMailbox->stats();
    if (lastMetricsSampleMs_ > 0 && now > lastMetricsSampleMs_) {
        const double seconds = static_cast<double>(now - lastMetricsSampleMs_) / 1000.0;
        decodeFps_ = static_cast<double>(decoded - lastDecodedSample_) / seconds;
        displayFps_ = static_cast<double>(
            mailboxStats.rendered - lastRenderedSample_
        ) / seconds;
    }
    lastMetricsSampleMs_ = now;
    lastDecodedSample_ = decoded;
    lastRenderedSample_ = mailboxStats.rendered;

    StreamMetrics metrics;
    metrics.streamId = streamId_;
    metrics.displayName = displayName_;
    metrics.state = stateName(state_);
    metrics.packetsReceived =
        decodeSession_->packetsReceived.load(std::memory_order_relaxed);
    metrics.packetBytesReceived =
        decodeSession_->packetBytesReceived.load(std::memory_order_relaxed);
    metrics.packetsDropped =
        decodeSession_->packetsDropped.load(std::memory_order_relaxed);
    metrics.decodedFrames = decoded;
    metrics.convertedFrames =
        decodeSession_->convertedFrames.load(std::memory_order_relaxed);
    metrics.submittedFrames = mailboxStats.submitted;
    metrics.mailboxOverwrittenFrames = mailboxStats.overwritten;
    metrics.unsupportedFrames =
        decodeSession_->unsupportedFrames.load(std::memory_order_relaxed);
    metrics.markerDecodedFrames =
        decodeSession_->markerDecodedFrames.load(std::memory_order_relaxed);
    metrics.markerDecodeFailures =
        decodeSession_->markerDecodeFailures.load(std::memory_order_relaxed);
    metrics.sourceSequenceGaps =
        decodeSession_->sourceSequenceGaps.load(std::memory_order_relaxed);
    metrics.uploadedFrames = mailboxStats.uploaded;
    metrics.renderedFrames = mailboxStats.rendered;
    metrics.presentedFrames = mailboxStats.rendered;
    metrics.uploadCpuUs = mailboxStats.uploadCpuUs;
    metrics.paintCpuUs = mailboxStats.paintCpuUs;
    metrics.dirtyMerges = mailboxStats.dirtyMerges;
    metrics.scheduleChecks = mailboxStats.scheduleChecks;
    metrics.textureBytes = mailboxStats.textureBytes;
    metrics.reconnectCount =
        decodeSession_->reconnectCount.load(std::memory_order_relaxed);
    metrics.decodeFps = decodeFps_;
    metrics.displayFps = displayFps_;

    {
        const std::lock_guard<std::mutex> lock(decodeSession_->queueMutex);
        metrics.queuePackets = static_cast<int>(decodeSession_->packets.size());
        metrics.queueBytes = decodeSession_->queueBytes;
    }
    const qint64 lastFrame =
        decodeSession_->lastFrameMonotonicMs.load(std::memory_order_relaxed);
    metrics.lastFrameAgeMs = lastFrame >= 0 ? std::max<qint64>(0, now - lastFrame) : -1;

    metrics.internalLatencyP95Ms = mailboxStats.internalLatencyP95Ms;
    metrics.sourceLatencyP50Ms = mailboxStats.sourceLatencyP50Ms;
    metrics.sourceLatencyP95Ms = mailboxStats.sourceLatencyP95Ms;
    metrics.sourceLatencyMaxMs = mailboxStats.sourceLatencyMaxMs;
    metrics.sourceLatencySamples = mailboxStats.sourceLatencySamples;
    metrics.presentationIntervalP50Ms =
        mailboxStats.presentationIntervalP50Ms;
    metrics.presentationIntervalP95Ms =
        mailboxStats.presentationIntervalP95Ms;
    metrics.presentationIntervalMaxMs =
        mailboxStats.presentationIntervalMaxMs;
    metrics.lastPresentedSourceSequence =
        mailboxStats.lastPresentedSourceSequence;
    return metrics;
}

void FFmpegPlayer::decodeNetworkLoop(QString rtmpUrl, std::uint64_t sessionId)
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
        const FfmpegInputResult inputResult = inputSession.run(rtmpUrl, sessionId);

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

        decodeSession_->reconnectCount.fetch_add(1, std::memory_order_relaxed);
        postState(DeviceStatus::Reconnecting, sessionId);
        postReconnectScheduled(
            consecutiveFailures,
            reconnectDelayMs,
            sessionId
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
    const auto configuration =
        std::static_pointer_cast<FfmpegCodecConfiguration>(codecConfiguration);
    const std::lock_guard<std::mutex> lock(decodeSession_->queueMutex);
    if (decodeSession_->stopping ||
        sessionId != sessionId_.load(std::memory_order_acquire)) {
        return;
    }

    decodeSession_->packetsDropped.fetch_add(
        decodeSession_->packets.size(), std::memory_order_relaxed
    );
    decodeSession_->packets.clear();
    decodeSession_->queueBytes = 0;
    decodeSession_->codecConfiguration = configuration;
    decodeSession_->configurationSessionId = sessionId;
    decodeSession_->decoderResetRequired = true;
    decodeSession_->dropUntilKeyframe = false;
    decodeSession_->playingSessionId.store(0, std::memory_order_release);
    scheduleDecodeLocked(decodeSession_);
}

void FFmpegPlayer::enqueuePacket(
    void *packetPointer,
    qint64 receivedMonotonicMs,
    std::uint64_t sessionId
)
{
    AVPacket *packet = static_cast<AVPacket *>(packetPointer);
    auto encodedPacket = std::make_shared<EncodedPacket>();
    encodedPacket->packet = packet;
    encodedPacket->receivedMonotonicMs = receivedMonotonicMs;
    encodedPacket->sessionId = sessionId;

    const qint64 packetSize = std::max(packet->size, 0);
    const bool keyFrame = (packet->flags & AV_PKT_FLAG_KEY) != 0;
    const std::lock_guard<std::mutex> lock(decodeSession_->queueMutex);
    if (decodeSession_->stopping ||
        sessionId != sessionId_.load(std::memory_order_acquire)) {
        return;
    }

    decodeSession_->packetsReceived.fetch_add(1, std::memory_order_relaxed);
    decodeSession_->packetBytesReceived.fetch_add(
        static_cast<std::uint64_t>(packetSize), std::memory_order_relaxed
    );

    if (decodeSession_->dropUntilKeyframe && !keyFrame) {
        decodeSession_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const bool wouldOverflow =
        static_cast<int>(decodeSession_->packets.size()) >=
            decodeSession_->options.maximumQueuedPackets ||
        decodeSession_->queueBytes + packetSize >
            decodeSession_->options.maximumQueuedBytes;
    if (wouldOverflow) {
        decodeSession_->packetsDropped.fetch_add(
            decodeSession_->packets.size(), std::memory_order_relaxed
        );
        decodeSession_->packets.clear();
        decodeSession_->queueBytes = 0;
        decodeSession_->decoderResetRequired = true;
        decodeSession_->dropUntilKeyframe = !keyFrame;
        if (!keyFrame) {
            decodeSession_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
            scheduleDecodeLocked(decodeSession_);
            return;
        }
    }

    decodeSession_->dropUntilKeyframe = false;
    decodeSession_->queueBytes += packetSize;
    decodeSession_->packets.push_back(std::move(encodedPacket));
    scheduleDecodeLocked(decodeSession_);
}

void FFmpegPlayer::scheduleDecodeLocked(
    const std::shared_ptr<StreamDecodeSession> &state
)
{
    if (state->decodeScheduled) {
        return;
    }
    state->decodeScheduled = true;
    if (!state->pool->post(state->workerIndex, [state] {
            drainDecodeSession(state);
        })) {
        state->decodeScheduled = false;
        state->idleCondition.notify_all();
    }
}

void FFmpegPlayer::drainDecodeSession(
    const std::shared_ptr<StreamDecodeSession> &state
)
{
    const qint64 batchStarted = monotonicMilliseconds();
    QString decodeError;

    bool resetDecoder = false;
    bool stopping = false;
    std::shared_ptr<FfmpegCodecConfiguration> configuration;
    std::vector<std::shared_ptr<EncodedPacket>> batch;
    std::vector<std::shared_ptr<EncodedPacket>> deferredBatch;
    {
        const std::lock_guard<std::mutex> lock(state->queueMutex);
        stopping = state->stopping;
        resetDecoder = state->decoderResetRequired;
        state->decoderResetRequired = false;
        configuration = state->codecConfiguration;
        while (!state->packets.empty() &&
               static_cast<int>(batch.size()) < kDecodeBatchPackets) {
            auto packet = std::move(state->packets.front());
            state->packets.pop_front();
            state->queueBytes -= std::max(packet->packet->size, 0);
            batch.push_back(std::move(packet));
        }
    }

    if (stopping) {
        state->decoder.reset();
    } else {
        if (resetDecoder) {
            state->decoder.reset();
            if (configuration != nullptr &&
                configuration->parameters != nullptr) {
                const AVCodec *decoder =
                    avcodec_find_decoder(configuration->parameters->codec_id);
                state->decoder.codecContext =
                    decoder != nullptr ? avcodec_alloc_context3(decoder) : nullptr;
                if (decoder == nullptr || state->decoder.codecContext == nullptr) {
                    decodeError = QObject::tr("无法创建 H.264 解码器。");
                } else if (avcodec_parameters_to_context(
                               state->decoder.codecContext,
                               configuration->parameters
                           ) < 0) {
                    decodeError = QObject::tr("复制解码参数失败。");
                } else {
                    state->decoder.codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
                    state->decoder.codecContext->thread_count = 1;
                    if (avcodec_open2(
                            state->decoder.codecContext, decoder, nullptr
                        ) < 0) {
                        decodeError = QObject::tr("打开 H.264 解码器失败。");
                    } else {
                        state->decoder.frame = av_frame_alloc();
                        state->decoder.sessionId =
                            state->configurationSessionId;
                        if (state->decoder.frame == nullptr) {
                            decodeError = QObject::tr("无法分配解码帧。");
                        }
                    }
                }
            }
        }

        auto receiveFrames =
            [&state, &decodeError, &configuration](qint64 receivedMonotonicMs) {
                while (decodeError.isEmpty()) {
                    const int receiveStatus = avcodec_receive_frame(
                        state->decoder.codecContext, state->decoder.frame
                    );
                    if (receiveStatus == AVERROR(EAGAIN) ||
                        receiveStatus == AVERROR_EOF) {
                        return;
                    }
                    if (receiveStatus < 0) {
                        decodeError = QObject::tr(
                            "解码视频帧失败：%1"
                        ).arg(ffmpegError(receiveStatus));
                        return;
                    }

                    AVFrame *frame = state->decoder.frame;
                    state->decodedFrames.fetch_add(1, std::memory_order_relaxed);
                    const qint64 now = monotonicMilliseconds();
                    if (state->decoder.diagnosticWidth != frame->width ||
                        state->decoder.diagnosticHeight != frame->height ||
                        state->decoder.diagnosticFormat != frame->format) {
                        state->decoder.diagnosticWidth = frame->width;
                        state->decoder.diagnosticHeight = frame->height;
                        state->decoder.diagnosticFormat = frame->format;
                        state->decoder.metadataFallbackReported = false;
                        state->decoder.unsupportedTransferReported = false;
                    }
                    const bool missingColorMetadata =
                        frame->color_primaries == AVCOL_PRI_UNSPECIFIED ||
                        frame->color_trc == AVCOL_TRC_UNSPECIFIED ||
                        frame->colorspace == AVCOL_SPC_UNSPECIFIED ||
                        frame->color_range == AVCOL_RANGE_UNSPECIFIED;
                    if (missingColorMetadata &&
                        !state->decoder.metadataFallbackReported) {
                        qWarning().noquote()
                            << "Stream" << state->streamId
                            << "has incomplete color metadata; applying the"
                               " documented SD/HD limited-range fallback.";
                        state->decoder.metadataFallbackReported = true;
                    }
                    AVFrame *renderFrame = normalizeForRendering(
                        state->decoder, frame
                    );
                    if (renderFrame != frame) {
                        state->convertedFrames.fetch_add(
                            1, std::memory_order_relaxed
                        );
                    }
                    const std::uint64_t sequence =
                        state->frameSequence.fetch_add(
                            1, std::memory_order_relaxed
                        ) +
                        1;
                    const VideoRational timeBase {
                        configuration != nullptr
                            ? configuration->timeBase.num
                            : 0,
                        configuration != nullptr
                            ? configuration->timeBase.den
                            : 1
                    };
                    qint64 sourceTimestampMs = -1;
                    std::optional<std::uint32_t> sourceSequence;
                    if (state->options.latencyMarkerEnabled) {
                        const LatencyMarkerDecodeResult marker =
                            LatencyMarkerCodec::decode({
                                renderFrame->data[0],
                                renderFrame->width,
                                renderFrame->height,
                                renderFrame->linesize[0]
                            });
                        if (marker.sourceTimestampMs.has_value()) {
                            sourceTimestampMs = *marker.sourceTimestampMs;
                            state->markerDecodedFrames.fetch_add(
                                1, std::memory_order_relaxed
                            );
                        } else {
                            state->markerDecodeFailures.fetch_add(
                                1, std::memory_order_relaxed
                            );
                        }
                        sourceSequence = marker.sourceSequence;
                        if (sourceSequence.has_value()) {
                            if (state->decoder.hasLastSourceSequence) {
                                const std::uint32_t delta =
                                    *sourceSequence -
                                    state->decoder.lastSourceSequence;
                                if (delta > 1U && delta < 0x80000000U) {
                                    state->sourceSequenceGaps.fetch_add(
                                        delta - 1U,
                                        std::memory_order_relaxed
                                    );
                                }
                            }
                            state->decoder.hasLastSourceSequence = true;
                            state->decoder.lastSourceSequence =
                                *sourceSequence;
                        }
                    }
                    const auto videoFrame = FfmpegVideoFrameAdapter::adapt(
                        renderFrame,
                        timeBase,
                        sequence,
                        state->decoder.sessionId,
                        receivedMonotonicMs,
                        sourceTimestampMs,
                        sourceSequence
                    );
                    av_frame_unref(frame);
                    if (!videoFrame.has_value() ||
                        !isSupportedSdrTransfer(videoFrame->color().transfer)) {
                        if (videoFrame.has_value() &&
                            !state->decoder.unsupportedTransferReported) {
                            qWarning().noquote()
                                << "Stream" << state->streamId
                                << "uses an unsupported HDR transfer; frame"
                                   " rendering is disabled for this format.";
                            state->decoder.unsupportedTransferReported = true;
                        }
                        state->unsupportedFrames.fetch_add(
                            1, std::memory_order_relaxed
                        );
                        continue;
                    }
                    (void)state->frameMailbox->submit(*videoFrame);
                    state->lastFrameMonotonicMs.store(
                        now, std::memory_order_relaxed
                    );

                    if (state->playingSessionId.exchange(
                            state->decoder.sessionId,
                            std::memory_order_acq_rel
                        ) != state->decoder.sessionId &&
                        state->owner != nullptr) {
                        state->owner->postState(
                            DeviceStatus::Playing,
                            state->decoder.sessionId
                        );
                    }
                }
            };

        if (decodeError.isEmpty() &&
            state->decoder.codecContext != nullptr &&
            state->decoder.frame != nullptr) {
            for (std::size_t index = 0; index < batch.size(); ++index) {
                if (index > 0 &&
                    monotonicMilliseconds() - batchStarted >=
                        kDecodeBatchMilliseconds) {
                    deferredBatch.insert(
                        deferredBatch.end(),
                        batch.begin() + static_cast<std::ptrdiff_t>(index),
                        batch.end()
                    );
                    break;
                }
                const auto &packet = batch.at(index);
                if (packet->sessionId != state->decoder.sessionId) {
                    continue;
                }
                int status = avcodec_send_packet(
                    state->decoder.codecContext, packet->packet
                );
                if (status == AVERROR(EAGAIN)) {
                    receiveFrames(packet->receivedMonotonicMs);
                    status = avcodec_send_packet(
                        state->decoder.codecContext, packet->packet
                    );
                }
                if (status < 0) {
                    decodeError = QObject::tr(
                        "提交 H.264 数据包失败：%1"
                    ).arg(ffmpegError(status));
                    break;
                }
                receiveFrames(packet->receivedMonotonicMs);
            }
        }
    }

    if (!decodeError.isEmpty() && state->owner != nullptr) {
        FFmpegPlayer *owner = state->owner;
        const std::uint64_t session = state->configurationSessionId;
        owner->postError(
            {
                PlaybackErrorCode::DecodeFailure,
                0,
                decodeError,
                true
            },
            session
        );
        owner->restartRequested_.store(true, std::memory_order_release);
        owner->reconnectCondition_.notify_all();
    }

    {
        const std::lock_guard<std::mutex> lock(state->queueMutex);
        if (!state->stopping && decodeError.isEmpty() &&
            !state->decoderResetRequired) {
            for (auto packet = deferredBatch.rbegin();
                 packet != deferredBatch.rend();
                 ++packet) {
                state->queueBytes +=
                    std::max((*packet)->packet->size, 0);
                state->packets.push_front(std::move(*packet));
            }
        } else {
            state->packetsDropped.fetch_add(
                deferredBatch.size(), std::memory_order_relaxed
            );
        }
        if (state->stopping) {
            state->packets.clear();
            state->queueBytes = 0;
            state->decoder.reset();
            state->decodeScheduled = false;
            state->idleCondition.notify_all();
        } else if (!state->packets.empty() ||
                   state->decoderResetRequired) {
            if (!state->pool->post(state->workerIndex, [state] {
                    drainDecodeSession(state);
                })) {
                state->decodeScheduled = false;
                state->idleCondition.notify_all();
            }
        } else {
            state->decodeScheduled = false;
            state->idleCondition.notify_all();
        }
    }
}

void FFmpegPlayer::postState(
    DeviceStatus state,
    std::uint64_t sessionId
)
{
    QMetaObject::invokeMethod(
        this,
        [this, state, sessionId] {
            if (sessionId == sessionId_.load(std::memory_order_acquire)) {
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
            if (sessionId == sessionId_.load(std::memory_order_acquire)) {
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
            if (sessionId == sessionId_.load(std::memory_order_acquire)) {
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
