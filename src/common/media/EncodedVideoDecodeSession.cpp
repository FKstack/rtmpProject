#include "media/EncodedVideoDecodeSession.h"

#include "FfmpegSessionTypes.h"
#include "FfmpegVideoFrameAdapter.h"
#include "media/DecodeWorkerPool.h"
#include "media/LatencyMarkerCodec.h"
#include "media/LatestFrameMailbox.h"

#include <QDebug>
#include <QObject>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
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

struct EncodedPacket
{
    AVPacket *packet = nullptr;
    qint64 receivedMonotonicMs = 0;
    std::uint64_t generation = 0;

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
    std::uint64_t generation = 0;
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
        generation = 0;
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

} // namespace

struct EncodedVideoDecodeSession::State
{
    StreamId streamId = kInvalidStreamId;
    QString displayName;
    DecodeWorkerPool *pool = nullptr;
    int workerIndex = 0;
    PlaybackPerformanceOptions options;
    StateCallback stateCallback;
    ErrorCallback errorCallback;

    std::mutex queueMutex;
    std::condition_variable idleCondition;
    std::deque<std::shared_ptr<EncodedPacket>> packets;
    qint64 queueBytes = 0;
    std::shared_ptr<FfmpegCodecConfiguration> codecConfiguration;
    std::uint64_t configurationGeneration = 0;
    bool decoderResetRequired = false;
    bool dropUntilKeyframe = false;
    bool decodeScheduled = false;
    bool stopping = false;
    DecoderRuntime decoder;

    std::shared_ptr<LatestFrameMailbox> frameMailbox =
        std::make_shared<LatestFrameMailbox>();
    std::atomic_uint64_t activeGeneration {0};
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
    std::atomic_uint64_t playingGeneration {0};
    std::atomic<qint64> lastFrameMonotonicMs {-1};

    qint64 lastMetricsSampleMs = 0;
    std::uint64_t lastDecodedSample = 0;
    std::uint64_t lastRenderedSample = 0;
    double decodeFps = 0.0;
    double displayFps = 0.0;
};

EncodedVideoDecodeSession::EncodedVideoDecodeSession(
    StreamId streamId,
    QString displayName,
    DecodeWorkerPool *decodeWorkerPool,
    PlaybackPerformanceOptions options,
    StateCallback stateCallback,
    ErrorCallback errorCallback
)
    : state_(std::make_shared<State>())
{
    state_->streamId = streamId;
    state_->displayName = std::move(displayName);
    state_->pool = decodeWorkerPool;
    state_->workerIndex = decodeWorkerPool != nullptr
        ? decodeWorkerPool->workerIndexFor(streamId)
        : 0;
    state_->options = options;
    state_->options.maximumQueuedPackets =
        std::max(1, state_->options.maximumQueuedPackets);
    state_->options.maximumQueuedBytes =
        std::max<qint64>(1, state_->options.maximumQueuedBytes);
    state_->stateCallback = std::move(stateCallback);
    state_->errorCallback = std::move(errorCallback);
}

void EncodedVideoDecodeSession::attachDecodeWorkerPool(
    DecodeWorkerPool *decodeWorkerPool
)
{
    Q_ASSERT(decodeWorkerPool != nullptr);
    Q_ASSERT(activeGeneration() == 0);
    const std::lock_guard<std::mutex> lock(state_->queueMutex);
    Q_ASSERT(!state_->decodeScheduled);
    state_->pool = decodeWorkerPool;
    state_->workerIndex = decodeWorkerPool != nullptr
        ? decodeWorkerPool->workerIndexFor(state_->streamId)
        : 0;
}

EncodedVideoDecodeSession::~EncodedVideoDecodeSession()
{
    close();
}

bool EncodedVideoDecodeSession::beginExternalGeneration(
    std::uint64_t generation
)
{
    if (generation == 0 || state_->pool == nullptr) {
        return false;
    }
    prepareGeneration(generation, true);

    auto configuration = std::make_shared<FfmpegCodecConfiguration>();
    if (configuration->parameters == nullptr) {
        closeGeneration(generation);
        return false;
    }
    configuration->parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    configuration->parameters->codec_id = AV_CODEC_ID_H264;
    configuration->timeBase = {1, 1'000'000};
    configuration->kind = FfmpegTrackKind::Video;
    configureOpaque(
        std::static_pointer_cast<void>(configuration), generation, true
    );
    if (state_->stateCallback) {
        state_->stateCallback(DeviceStatus::Connecting, generation);
    }
    return true;
}

void EncodedVideoDecodeSession::prepareGeneration(
    std::uint64_t generation,
    bool waitForKeyframe
)
{
    close();
    if (generation == 0) {
        return;
    }

    {
        const std::lock_guard<std::mutex> lock(state_->queueMutex);
        state_->packets.clear();
        state_->queueBytes = 0;
        state_->codecConfiguration.reset();
        state_->configurationGeneration = 0;
        state_->decoderResetRequired = true;
        state_->dropUntilKeyframe = waitForKeyframe;
        state_->stopping = false;
        state_->playingGeneration.store(0, std::memory_order_release);
    }
    state_->frameMailbox->clear();
    state_->activeGeneration.store(generation, std::memory_order_release);
}

H264SubmitResult EncodedVideoDecodeSession::submit(SessionMediaSample sample)
{
    const std::uint64_t current =
        state_->activeGeneration.load(std::memory_order_acquire);
    if (current == 0) {
        return H264SubmitResult::Closed;
    }
    if (sample.generation == 0 || sample.generation != current) {
        return H264SubmitResult::InvalidGeneration;
    }
    const std::size_t maximumBytes = static_cast<std::size_t>(
        state_->options.maximumQueuedBytes
    );
    if (sample.accessUnit.annexB.size() > maximumBytes) {
        state_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
        return H264SubmitResult::DroppedCapacity;
    }
    if (sample.accessUnit.annexB.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        state_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
        return H264SubmitResult::DroppedCapacity;
    }
    if (!isValidH264AccessUnit(sample.accessUnit, maximumBytes)) {
        state_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
        return H264SubmitResult::InvalidAccessUnit;
    }

    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr ||
        av_new_packet(
            packet,
            static_cast<int>(sample.accessUnit.annexB.size())
        ) < 0) {
        av_packet_free(&packet);
        state_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
        return H264SubmitResult::ResourceFailure;
    }
    std::memcpy(
        packet->data,
        sample.accessUnit.annexB.data(),
        sample.accessUnit.annexB.size()
    );
    packet->pts = sample.accessUnit.mediaTimestampUs;
    packet->dts = sample.accessUnit.mediaTimestampUs;
    if (sample.accessUnit.keyFrame) {
        packet->flags |= AV_PKT_FLAG_KEY;
    }
    return submitOpaquePacket(
        packet, monotonicMilliseconds(), sample.generation
    );
}

void EncodedVideoDecodeSession::configureOpaque(
    const std::shared_ptr<void> &codecConfiguration,
    std::uint64_t generation,
    bool waitForKeyframe
)
{
    const auto configuration =
        std::static_pointer_cast<FfmpegCodecConfiguration>(codecConfiguration);
    const std::lock_guard<std::mutex> lock(state_->queueMutex);
    if (state_->stopping || generation == 0 ||
        generation !=
            state_->activeGeneration.load(std::memory_order_acquire)) {
        return;
    }

    state_->packetsDropped.fetch_add(
        state_->packets.size(), std::memory_order_relaxed
    );
    state_->packets.clear();
    state_->queueBytes = 0;
    state_->codecConfiguration = configuration;
    state_->configurationGeneration = generation;
    state_->decoderResetRequired = true;
    state_->dropUntilKeyframe = waitForKeyframe;
    state_->playingGeneration.store(0, std::memory_order_release);
    scheduleDecodeLocked(state_);
}

H264SubmitResult EncodedVideoDecodeSession::submitOpaquePacket(
    void *packetPointer,
    qint64 receivedMonotonicMs,
    std::uint64_t generation
)
{
    AVPacket *packet = static_cast<AVPacket *>(packetPointer);
    if (packet == nullptr) {
        return H264SubmitResult::InvalidAccessUnit;
    }

    auto encodedPacket = std::make_shared<EncodedPacket>();
    encodedPacket->packet = packet;
    encodedPacket->receivedMonotonicMs = receivedMonotonicMs;
    encodedPacket->generation = generation;

    const qint64 packetSize = std::max(packet->size, 0);
    const bool keyFrame = (packet->flags & AV_PKT_FLAG_KEY) != 0;
    const std::lock_guard<std::mutex> lock(state_->queueMutex);
    const std::uint64_t current =
        state_->activeGeneration.load(std::memory_order_acquire);
    if (current == 0 || state_->stopping) {
        return H264SubmitResult::Closed;
    }
    if (generation == 0 || generation != current) {
        return H264SubmitResult::InvalidGeneration;
    }

    state_->packetsReceived.fetch_add(1, std::memory_order_relaxed);
    state_->packetBytesReceived.fetch_add(
        static_cast<std::uint64_t>(packetSize), std::memory_order_relaxed
    );
    if (packetSize > state_->options.maximumQueuedBytes) {
        state_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
        return H264SubmitResult::DroppedCapacity;
    }
    if (state_->dropUntilKeyframe && !keyFrame) {
        state_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
        return H264SubmitResult::DroppedUntilKeyframe;
    }

    const bool wouldOverflow =
        static_cast<int>(state_->packets.size()) >=
            state_->options.maximumQueuedPackets ||
        state_->queueBytes + packetSize >
            state_->options.maximumQueuedBytes;
    H264SubmitResult result = H264SubmitResult::Accepted;
    if (wouldOverflow) {
        state_->packetsDropped.fetch_add(
            state_->packets.size(), std::memory_order_relaxed
        );
        state_->packets.clear();
        state_->queueBytes = 0;
        state_->decoderResetRequired = true;
        state_->dropUntilKeyframe = !keyFrame;
        if (!keyFrame) {
            state_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
            scheduleDecodeLocked(state_);
            return H264SubmitResult::DroppedCapacity;
        }
        result = H264SubmitResult::AcceptedAfterDrop;
    }

    state_->dropUntilKeyframe = false;
    state_->queueBytes += packetSize;
    state_->packets.push_back(std::move(encodedPacket));
    scheduleDecodeLocked(state_);
    return result;
}

void EncodedVideoDecodeSession::scheduleDecodeLocked(
    const std::shared_ptr<State> &state
)
{
    if (state->decodeScheduled || state->pool == nullptr) {
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

void EncodedVideoDecodeSession::drainDecodeSession(
    const std::shared_ptr<State> &state
)
{
    const qint64 batchStarted = monotonicMilliseconds();
    QString decodeError;

    bool resetDecoder = false;
    bool stopping = false;
    std::uint64_t configuredGeneration = 0;
    std::shared_ptr<FfmpegCodecConfiguration> configuration;
    std::vector<std::shared_ptr<EncodedPacket>> batch;
    std::vector<std::shared_ptr<EncodedPacket>> deferredBatch;
    {
        const std::lock_guard<std::mutex> lock(state->queueMutex);
        stopping = state->stopping;
        resetDecoder = state->decoderResetRequired;
        state->decoderResetRequired = false;
        configuration = state->codecConfiguration;
        configuredGeneration = state->configurationGeneration;
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
                state->decoder.codecContext = decoder != nullptr
                    ? avcodec_alloc_context3(decoder)
                    : nullptr;
                if (decoder == nullptr ||
                    state->decoder.codecContext == nullptr) {
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
                        state->decoder.generation =
                            configuredGeneration;
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
                        state->decoder.generation,
                        receivedMonotonicMs,
                        sourceTimestampMs,
                        sourceSequence
                    );
                    av_frame_unref(frame);
                    const bool currentGeneration =
                        state->activeGeneration.load(
                            std::memory_order_acquire
                        ) == state->decoder.generation;
                    if (!currentGeneration) {
                        continue;
                    }
                    if (!videoFrame.has_value() ||
                        !isSupportedSdrTransfer(
                            videoFrame->color().transfer
                        )) {
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

                    if (state->playingGeneration.exchange(
                            state->decoder.generation,
                            std::memory_order_acq_rel
                        ) != state->decoder.generation &&
                        state->stateCallback) {
                        state->stateCallback(
                            DeviceStatus::Playing,
                            state->decoder.generation
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
                if (packet->generation != state->decoder.generation ||
                    packet->generation != state->activeGeneration.load(
                        std::memory_order_acquire
                    )) {
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

    const std::uint64_t errorGeneration = configuredGeneration;
    if (!decodeError.isEmpty() &&
        errorGeneration != 0 &&
        errorGeneration == state->activeGeneration.load(
            std::memory_order_acquire
        ) &&
        state->errorCallback) {
        state->errorCallback(
            {
                PlaybackErrorCode::DecodeFailure,
                0,
                decodeError,
                true
            },
            errorGeneration
        );
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

        if (!decodeError.isEmpty()) {
            state->packetsDropped.fetch_add(
                state->packets.size(), std::memory_order_relaxed
            );
            state->packets.clear();
            state->queueBytes = 0;
            state->decoder.reset();
            state->decoderResetRequired = true;
            state->dropUntilKeyframe = true;
            state->decodeScheduled = false;
            state->idleCondition.notify_all();
        } else if (state->stopping) {
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

void EncodedVideoDecodeSession::closeGeneration(std::uint64_t generation)
{
    if (generation == 0) {
        return;
    }
    std::uint64_t expected = generation;
    if (!state_->activeGeneration.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel
        )) {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(state_->queueMutex);
        state_->stopping = true;
        state_->packetsDropped.fetch_add(
            state_->packets.size(), std::memory_order_relaxed
        );
        state_->packets.clear();
        state_->queueBytes = 0;
        state_->decoderResetRequired = true;
        if (state_->decodeScheduled) {
            state_->idleCondition.wait(lock, [this] {
                return !state_->decodeScheduled;
            });
        } else {
            state_->decoder.reset();
        }
        state_->codecConfiguration.reset();
        state_->configurationGeneration = 0;
        state_->decoderResetRequired = false;
        state_->dropUntilKeyframe = false;
        state_->playingGeneration.store(0, std::memory_order_release);
        state_->stopping = false;
    }
    state_->frameMailbox->clear();
    if (state_->stateCallback) {
        state_->stateCallback(DeviceStatus::Disconnected, generation);
    }
}

void EncodedVideoDecodeSession::close()
{
    const std::uint64_t generation =
        state_->activeGeneration.load(std::memory_order_acquire);
    closeGeneration(generation);
}

std::uint64_t EncodedVideoDecodeSession::activeGeneration() const noexcept
{
    return state_->activeGeneration.load(std::memory_order_acquire);
}

std::shared_ptr<LatestFrameMailbox>
EncodedVideoDecodeSession::frameMailbox() const
{
    return state_->frameMailbox;
}

StreamMetrics EncodedVideoDecodeSession::metricsSnapshot(DeviceStatus state)
{
    const qint64 now = monotonicMilliseconds();
    const std::uint64_t decoded =
        state_->decodedFrames.load(std::memory_order_relaxed);
    const LatestFrameMailboxStats mailboxStats = state_->frameMailbox->stats();
    if (state_->lastMetricsSampleMs > 0 &&
        now > state_->lastMetricsSampleMs) {
        const double seconds = static_cast<double>(
            now - state_->lastMetricsSampleMs
        ) / 1000.0;
        state_->decodeFps = static_cast<double>(
            decoded - state_->lastDecodedSample
        ) / seconds;
        state_->displayFps = static_cast<double>(
            mailboxStats.rendered - state_->lastRenderedSample
        ) / seconds;
    }
    state_->lastMetricsSampleMs = now;
    state_->lastDecodedSample = decoded;
    state_->lastRenderedSample = mailboxStats.rendered;

    StreamMetrics metrics;
    metrics.streamId = state_->streamId;
    metrics.displayName = state_->displayName;
    metrics.state = stateName(state);
    metrics.packetsReceived =
        state_->packetsReceived.load(std::memory_order_relaxed);
    metrics.packetBytesReceived =
        state_->packetBytesReceived.load(std::memory_order_relaxed);
    metrics.packetsDropped =
        state_->packetsDropped.load(std::memory_order_relaxed);
    metrics.decodedFrames = decoded;
    metrics.convertedFrames =
        state_->convertedFrames.load(std::memory_order_relaxed);
    metrics.submittedFrames = mailboxStats.submitted;
    metrics.mailboxOverwrittenFrames = mailboxStats.overwritten;
    metrics.unsupportedFrames =
        state_->unsupportedFrames.load(std::memory_order_relaxed);
    metrics.markerDecodedFrames =
        state_->markerDecodedFrames.load(std::memory_order_relaxed);
    metrics.markerDecodeFailures =
        state_->markerDecodeFailures.load(std::memory_order_relaxed);
    metrics.sourceSequenceGaps =
        state_->sourceSequenceGaps.load(std::memory_order_relaxed);
    metrics.uploadedFrames = mailboxStats.uploaded;
    metrics.renderedFrames = mailboxStats.rendered;
    metrics.presentedFrames = mailboxStats.rendered;
    metrics.uploadCpuUs = mailboxStats.uploadCpuUs;
    metrics.paintCpuUs = mailboxStats.paintCpuUs;
    metrics.dirtyMerges = mailboxStats.dirtyMerges;
    metrics.scheduleChecks = mailboxStats.scheduleChecks;
    metrics.textureBytes = mailboxStats.textureBytes;
    metrics.reconnectCount =
        state_->reconnectCount.load(std::memory_order_relaxed);
    metrics.decodeFps = state_->decodeFps;
    metrics.displayFps = state_->displayFps;

    {
        const std::lock_guard<std::mutex> lock(state_->queueMutex);
        metrics.queuePackets = static_cast<int>(state_->packets.size());
        metrics.queueBytes = state_->queueBytes;
    }
    const qint64 lastFrame =
        state_->lastFrameMonotonicMs.load(std::memory_order_relaxed);
    metrics.lastFrameAgeMs = lastFrame >= 0
        ? std::max<qint64>(0, now - lastFrame)
        : -1;
    metrics.internalLatencyP50Ms = mailboxStats.internalLatencyP50Ms;
    metrics.internalLatencyP95Ms = mailboxStats.internalLatencyP95Ms;
    metrics.internalLatencyMaxMs = mailboxStats.internalLatencyMaxMs;
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

void EncodedVideoDecodeSession::recordReconnect() noexcept
{
    state_->reconnectCount.fetch_add(1, std::memory_order_relaxed);
}
