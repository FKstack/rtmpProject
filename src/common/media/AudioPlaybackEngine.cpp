#include "media/AudioPlaybackEngine.h"
#include "media/LatestFrameMailbox.h"

#include "FfmpegSessionTypes.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace {

constexpr int kOutputSampleRate = 48'000;
constexpr int kOutputChannels = 1;
constexpr int kBytesPerSample = 2;
constexpr int kRequestedBufferMs = 60;
constexpr int kMaximumPcmMs = 100;
constexpr int kUnderrunRecoveryPcmMs = 40;
constexpr int kMaximumPackets = 12;
constexpr qint64 kMaximumAudioLeadMs = 45;
constexpr qint64 kMaximumAudioLagMs = 125;
constexpr qint64 kMaximumPacketBytes = 256 * 1024;

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           ).count();
}

qint64 monotonicMicroseconds()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           ).count();
}

qint64 bytesToMilliseconds(qint64 bytes)
{
    return bytes * 1000 /
           (kOutputSampleRate * kOutputChannels * kBytesPerSample);
}

qint64 percentile(std::vector<qint64> samples, double fraction)
{
    if (samples.empty()) return -1;
    std::sort(samples.begin(), samples.end());
    const std::size_t index = static_cast<std::size_t>(
        fraction * static_cast<double>(samples.size() - 1)
    );
    return samples.at(index);
}

struct OwnedPacket
{
    AVPacket *packet = nullptr;
    StreamId streamId = kInvalidStreamId;
    qint64 receivedMonotonicMs = 0;
    std::uint64_t sessionId = 0;

    ~OwnedPacket() { av_packet_free(&packet); }
};

} // namespace

struct AudioPlaybackEngine::Runtime
{
    struct PendingProbe
    {
        qsizetype remainingBytes = 0;
        AudioPlaybackProbeSample sample;
    };

    explicit Runtime(AudioPlaybackEngine *ownerValue)
        : owner(ownerValue)
    {
    }

    AudioPlaybackEngine *owner = nullptr;
    QThread thread;
    QObject *workerContext = nullptr;

    mutable std::mutex mutex;
    std::deque<std::shared_ptr<OwnedPacket>> packets;
    qint64 queuedBytes = 0;
    std::unordered_map<StreamId, std::shared_ptr<FfmpegCodecConfiguration>> configurations;
    std::unordered_map<StreamId, std::uint64_t> configurationSessions;
    std::unordered_map<StreamId, AudioPlaybackState> states;
    std::unordered_map<StreamId, std::weak_ptr<LatestFrameMailbox>> videoClocks;
    std::weak_ptr<AudioPlaybackObserver> qualificationObserver;
    AudioPlaybackMetrics metrics;
    std::deque<qint64> outputLatencySamples;
    std::atomic<StreamId> selected {kInvalidStreamId};
    std::atomic_bool muted {true};
    std::atomic_bool stopping {false};
    std::atomic_bool drainPosted {false};

    AVCodecContext *decoder = nullptr;
    AVFrame *frame = nullptr;
    SwrContext *resampler = nullptr;
    std::uint64_t decoderSession = 0;
    StreamId decoderStream = kInvalidStreamId;
    AVRational audioTimeBase {0, 1};
    std::unique_ptr<QMediaDevices> mediaDevices;
    std::unique_ptr<QAudioSink> sink;
    QIODevice *sinkDevice = nullptr;
    qint64 sinkAudioStartedUs = -1;
    qint64 sinkUnderrunStartedUs = -1;
    qint64 sinkUnderrunAccumulatedUs = 0;
    QByteArray pendingPcm;
    std::deque<PendingProbe> pendingProbes;
    bool flushRetryPosted = false;
    bool recoveringFromUnderrun = false;

    void publishState(StreamId streamId, AudioPlaybackState state)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            const auto previous = states.find(streamId);
            if (previous != states.end() && previous->second == state) {
                return;
            }
            states[streamId] = state;
            if (streamId == selected.load(std::memory_order_acquire)) {
                metrics.streamId = streamId;
                metrics.state = state;
            }
        }
        QMetaObject::invokeMethod(
            owner,
            [owner = owner, streamId, state] {
                emit owner->stateChanged(streamId, state);
            },
            Qt::QueuedConnection
        );
    }

    void publishMetrics()
    {
        AudioPlaybackMetrics snapshot;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            metrics.queuedPackets = static_cast<int>(packets.size());
            metrics.queuedBytes = queuedBytes;
            metrics.pcmBufferedMs = bytesToMilliseconds(pendingPcm.size());
            const qint64 activeUnderrunUs = sinkUnderrunStartedUs >= 0
                ? monotonicMicroseconds() - sinkUnderrunStartedUs : 0;
            metrics.underrunDurationMs =
                (sinkUnderrunAccumulatedUs + activeUnderrunUs) / 1000;
            snapshot = metrics;
        }
        QMetaObject::invokeMethod(
            owner,
            [owner = owner, snapshot] { emit owner->metricsChanged(snapshot); },
            Qt::QueuedConnection
        );
    }

    void resetDecoder()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (sinkUnderrunStartedUs >= 0) {
                sinkUnderrunAccumulatedUs +=
                    monotonicMicroseconds() - sinkUnderrunStartedUs;
            }
            sinkAudioStartedUs = -1;
            sinkUnderrunStartedUs = -1;
            metrics.underrunDurationMs = sinkUnderrunAccumulatedUs / 1000;
        }
        pendingPcm.clear();
        pendingProbes.clear();
        flushRetryPosted = false;
        recoveringFromUnderrun = false;
        sinkDevice = nullptr;
        sink.reset();
        mediaDevices.reset();
        if (resampler != nullptr) swr_free(&resampler);
        if (frame != nullptr) av_frame_free(&frame);
        if (decoder != nullptr) avcodec_free_context(&decoder);
        decoderSession = 0;
        decoderStream = kInvalidStreamId;
        audioTimeBase = {0, 1};
    }

    bool ensureOutput()
    {
        if (sink != nullptr && sinkDevice != nullptr) return true;

        QAudioFormat format;
        format.setSampleRate(kOutputSampleRate);
        format.setChannelCount(kOutputChannels);
        format.setSampleFormat(QAudioFormat::Int16);
        if (mediaDevices == nullptr) {
            mediaDevices = std::make_unique<QMediaDevices>();
            QObject::connect(
                mediaDevices.get(),
                &QMediaDevices::audioOutputsChanged,
                workerContext,
                [this] {
                    sinkDevice = nullptr;
                    sink.reset();
                    const StreamId streamId = selected.load();
                    const bool recovered = ensureOutput();
                    if (streamId != kInvalidStreamId) {
                        publishState(
                            streamId,
                            recovered
                                ? (muted.load()
                                       ? AudioPlaybackState::Muted
                                       : AudioPlaybackState::Buffering)
                                : AudioPlaybackState::OutputError
                        );
                    }
                }
            );
        }
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.isFormatSupported(format)) {
            return false;
        }
        sink = std::make_unique<QAudioSink>(device, format);
        QObject::connect(
            sink.get(),
            &QAudioSink::stateChanged,
            workerContext,
            [this](QAudio::State state) {
                const qint64 nowUs = monotonicMicroseconds();
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    if (state == QAudio::IdleState && decoder != nullptr &&
                        sinkAudioStartedUs >= 0 &&
                        sinkUnderrunStartedUs < 0) {
                        sinkUnderrunStartedUs = nowUs;
                        recoveringFromUnderrun = true;
                        ++metrics.underruns;
                    } else if (state != QAudio::IdleState &&
                               sinkUnderrunStartedUs >= 0) {
                        sinkUnderrunAccumulatedUs +=
                            nowUs - sinkUnderrunStartedUs;
                        sinkUnderrunStartedUs = -1;
                        metrics.underrunDurationMs =
                            sinkUnderrunAccumulatedUs / 1000;
                    }
                }
                if (state == QAudio::StoppedState && sink != nullptr &&
                    sink->error() != QAudio::NoError) {
                    const StreamId streamId = selected.load();
                    if (streamId != kInvalidStreamId) {
                        publishState(streamId, AudioPlaybackState::OutputError);
                    }
                }
            }
        );
        const qsizetype requestedBytes =
            kOutputSampleRate * kOutputChannels * kBytesPerSample *
            kRequestedBufferMs / 1000;
        sink->setBufferSize(static_cast<int>(requestedBytes));
        sink->setVolume(muted.load(std::memory_order_acquire) ? 0.0 : 1.0);
        sinkDevice = sink->start();
        if (sinkDevice == nullptr) {
            sink.reset();
            return false;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex);
            metrics.actualSinkBufferMs = bytesToMilliseconds(sink->bufferSize());
        }
        return true;
    }

    bool configureDecoder(
        StreamId streamId,
        const std::shared_ptr<FfmpegCodecConfiguration> &configuration,
        std::uint64_t sessionId
    )
    {
        resetDecoder();
        if (configuration == nullptr || configuration->parameters == nullptr ||
            configuration->parameters->codec_id != AV_CODEC_ID_AAC) {
            return false;
        }
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
        decoder = codec != nullptr ? avcodec_alloc_context3(codec) : nullptr;
        if (decoder == nullptr ||
            avcodec_parameters_to_context(decoder, configuration->parameters) < 0) {
            resetDecoder();
            return false;
        }
        decoder->pkt_timebase = configuration->timeBase;
        if (avcodec_open2(decoder, codec, nullptr) < 0) {
            resetDecoder();
            return false;
        }
        frame = av_frame_alloc();
        if (frame == nullptr || !ensureOutput()) {
            resetDecoder();
            return false;
        }
        decoderStream = streamId;
        decoderSession = sessionId;
        audioTimeBase = configuration->timeBase;
        return true;
    }

    bool ensureResampler(const AVFrame *source)
    {
        if (resampler != nullptr) return true;
        AVChannelLayout outputLayout = AV_CHANNEL_LAYOUT_MONO;
        const int status = swr_alloc_set_opts2(
            &resampler,
            &outputLayout,
            AV_SAMPLE_FMT_S16,
            kOutputSampleRate,
            &source->ch_layout,
            static_cast<AVSampleFormat>(source->format),
            source->sample_rate,
            0,
            nullptr
        );
        return status >= 0 && resampler != nullptr && swr_init(resampler) >= 0;
    }

    void discardOldestPcmFrame()
    {
        if (pendingProbes.empty()) {
            pendingPcm.clear();
            return;
        }
        const qsizetype bytes = pendingProbes.front().remainingBytes;
        pendingPcm.remove(0, bytes);
        pendingProbes.pop_front();
        const std::lock_guard<std::mutex> lock(mutex);
        ++metrics.packetsDropped;
    }

    void trimExpiredAndOverflowingPcm(qint64 videoTimestampUs)
    {
        while (!pendingProbes.empty() && videoTimestampUs >= 0) {
            const qint64 audioTimestampUs =
                pendingProbes.front().sample.mediaPtsUs;
            if (audioTimestampUs < 0 ||
                audioTimestampUs - videoTimestampUs >=
                    -kMaximumAudioLagMs * 1000) {
                break;
            }
            discardOldestPcmFrame();
        }

        const qsizetype maximumBytes =
            kOutputSampleRate * kOutputChannels * kBytesPerSample *
            kMaximumPcmMs / 1000;
        while (pendingPcm.size() > maximumBytes &&
               !pendingProbes.empty()) {
            // Drop complete decoded frames. Removing an arbitrary byte prefix
            // creates a discontinuity inside a PCM sample and is audible as a
            // click on Windows audio devices.
            discardOldestPcmFrame();
        }
    }

    void flushPcm(qint64 videoTimestampUs)
    {
        if (sinkDevice == nullptr || pendingPcm.isEmpty()) return;
        const qsizetype recoveryBytes =
            kOutputSampleRate * kOutputChannels * kBytesPerSample *
            kUnderrunRecoveryPcmMs / 1000;
        if (recoveringFromUnderrun && pendingPcm.size() < recoveryBytes) {
            schedulePcmFlushRetry();
            return;
        }
        qsizetype releasableBytes = 0;
        for (const PendingProbe &probe : pendingProbes) {
            const qint64 audioTimestampUs = probe.sample.mediaPtsUs;
            if (videoTimestampUs >= 0 && audioTimestampUs >= 0 &&
                audioTimestampUs - videoTimestampUs >
                    kMaximumAudioLeadMs * 1000) {
                break;
            }
            releasableBytes += probe.remainingBytes;
        }
        if (releasableBytes <= 0) {
            schedulePcmFlushRetry();
            return;
        }
        const qint64 written = sinkDevice->write(
            pendingPcm.constData(), releasableBytes
        );
        if (written <= 0) {
            schedulePcmFlushRetry();
            return;
        }
        recoveringFromUnderrun = false;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (sinkAudioStartedUs < 0) {
                sinkAudioStartedUs = monotonicMicroseconds();
            }
        }
        pendingPcm.remove(0, static_cast<qsizetype>(written));
        qint64 remaining = written;
        while (remaining > 0 && !pendingProbes.empty()) {
            PendingProbe &probe = pendingProbes.front();
            const qint64 consumed = std::min<qint64>(
                remaining, probe.remainingBytes
            );
            probe.remainingBytes -= static_cast<qsizetype>(consumed);
            remaining -= consumed;
            if (probe.remainingBytes > 0) continue;
            probe.sample.sinkWriteMonotonicUs = monotonicMicroseconds();
            if (videoTimestampUs >= 0) {
                probe.sample.videoRenderedPtsUs = videoTimestampUs;
            }
            std::shared_ptr<AudioPlaybackObserver> observer;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                observer = qualificationObserver.lock();
            }
            if (observer != nullptr) {
                observer->onAudioPlaybackSample(probe.sample);
            }
            pendingProbes.pop_front();
        }
        if (!pendingPcm.isEmpty()) schedulePcmFlushRetry();
    }

    qint64 currentVideoTimestampUs()
    {
        std::shared_ptr<LatestFrameMailbox> videoClock;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            const auto found = videoClocks.find(decoderStream);
            if (found != videoClocks.end()) videoClock = found->second.lock();
        }
        const qint64 timestampMs = videoClock != nullptr
            ? videoClock->audioSyncMediaTimestampMs(decoderSession)
            : -1;
        return timestampMs >= 0 ? timestampMs * 1000 : -1;
    }

    void schedulePcmFlushRetry()
    {
        if (flushRetryPosted || pendingPcm.isEmpty() ||
            workerContext == nullptr) {
            return;
        }
        flushRetryPosted = true;
        QTimer::singleShot(
            5, Qt::PreciseTimer, workerContext,
            [this] {
                flushRetryPosted = false;
                if (pendingPcm.isEmpty() || sinkDevice == nullptr) return;
                const qint64 videoTimestampUs = currentVideoTimestampUs();
                trimExpiredAndOverflowingPcm(videoTimestampUs);
                flushPcm(videoTimestampUs);
            }
        );
    }

    void appendFrame(AVFrame *source, qint64 receivedMonotonicMs)
    {
        if (!ensureResampler(source)) return;
        const int outputSamples = static_cast<int>(av_rescale_rnd(
            swr_get_delay(resampler, source->sample_rate) + source->nb_samples,
            kOutputSampleRate,
            source->sample_rate,
            AV_ROUND_UP
        ));
        QByteArray pcm(
            outputSamples * kOutputChannels * kBytesPerSample,
            Qt::Uninitialized
        );
        auto *output = reinterpret_cast<std::uint8_t *>(pcm.data());
        const int converted = swr_convert(
            resampler,
            &output,
            outputSamples,
            const_cast<const std::uint8_t **>(source->extended_data),
            source->nb_samples
        );
        if (converted <= 0) return;
        pcm.resize(converted * kOutputChannels * kBytesPerSample);

        std::shared_ptr<LatestFrameMailbox> videoClock;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            const auto found = videoClocks.find(decoderStream);
            if (found != videoClocks.end()) videoClock = found->second.lock();
        }
        qint64 audioTimestampUs = -1;
        const qint64 timestamp =
            source->best_effort_timestamp != AV_NOPTS_VALUE
                ? source->best_effort_timestamp
                : source->pts;
        if (timestamp != AV_NOPTS_VALUE && audioTimeBase.num > 0 &&
            audioTimeBase.den > 0) {
            audioTimestampUs = av_rescale_q(
                timestamp, audioTimeBase, AVRational {1, 1'000'000}
            );
        }
        const qint64 videoTimestampMs = videoClock != nullptr
            ? videoClock->audioSyncMediaTimestampMs(decoderSession)
            : -1;
        pendingPcm.append(pcm);
        const qint64 decodedAtUs = monotonicMicroseconds();
        pendingProbes.push_back(PendingProbe {
            pcm.size(),
            AudioPlaybackProbeSample {
                decoderStream,
                decoderSession,
                audioTimestampUs,
                videoTimestampMs >= 0 ? videoTimestampMs * 1000 : -1,
                receivedMonotonicMs * 1000,
                decodedAtUs,
                monotonicMicroseconds(),
                -1,
                pcm.size()
            }
        });
        const qint64 videoTimestampUs = videoTimestampMs >= 0
            ? videoTimestampMs * 1000 : -1;
        trimExpiredAndOverflowingPcm(videoTimestampUs);
        // Gate each queued frame by its own timestamp. Gating the whole queue
        // by the newest frame makes a naturally leading audio track accumulate
        // and then flush in bursts, which causes repeated sink underruns.
        flushPcm(videoTimestampUs);
        {
            const std::lock_guard<std::mutex> lock(mutex);
            ++metrics.decodedPackets;
            const qint64 observed =
                std::max<qint64>(0, monotonicMilliseconds() - receivedMonotonicMs);
            outputLatencySamples.push_back(observed);
            if (outputLatencySamples.size() > 600) {
                outputLatencySamples.pop_front();
            }
            const std::vector<qint64> samples(
                outputLatencySamples.begin(), outputLatencySamples.end()
            );
            metrics.outputLatencyP50Ms = percentile(samples, 0.50);
            metrics.outputLatencyP95Ms = percentile(samples, 0.95);
        }
    }

    void decodePacket(const std::shared_ptr<OwnedPacket> &owned)
    {
        std::shared_ptr<FfmpegCodecConfiguration> configuration;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            const auto found = configurations.find(owned->streamId);
            const auto session = configurationSessions.find(owned->streamId);
            if (found == configurations.end() ||
                session == configurationSessions.end() ||
                session->second != owned->sessionId) {
                ++metrics.packetsDropped;
                return;
            }
            configuration = found->second;
        }
        if (decoderStream != owned->streamId ||
            decoderSession != owned->sessionId) {
            if (!configureDecoder(
                    owned->streamId, configuration, owned->sessionId
                )) {
                publishState(owned->streamId, AudioPlaybackState::OutputError);
                return;
            }
            publishState(
                owned->streamId,
                muted.load(std::memory_order_acquire)
                    ? AudioPlaybackState::Muted
                    : AudioPlaybackState::Buffering
            );
        }

        int status = avcodec_send_packet(decoder, owned->packet);
        while (status >= 0) {
            status = avcodec_receive_frame(decoder, frame);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) break;
            if (status < 0) {
                publishState(owned->streamId, AudioPlaybackState::OutputError);
                return;
            }
            appendFrame(frame, owned->receivedMonotonicMs);
            av_frame_unref(frame);
            publishState(
                owned->streamId,
                muted.load(std::memory_order_acquire)
                    ? AudioPlaybackState::Muted
                    : AudioPlaybackState::Playing
            );
        }
    }

    void drain()
    {
        for (;;) {
            std::shared_ptr<OwnedPacket> packet;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (packets.empty()) {
                    drainPosted.store(false, std::memory_order_release);
                    break;
                }
                packet = std::move(packets.front());
                packets.pop_front();
                queuedBytes -= std::max(packet->packet->size, 0);
            }
            if (packet->streamId == selected.load(std::memory_order_acquire)) {
                decodePacket(packet);
            }
        }
        publishMetrics();
    }
};

AudioPlaybackEngine::AudioPlaybackEngine(QObject *parent)
    : QObject(parent)
    , runtime_(std::make_unique<Runtime>(this))
{
    qRegisterMetaType<AudioPlaybackState>();
    qRegisterMetaType<AudioPlaybackMetrics>();
    runtime_->workerContext = new QObject();
    runtime_->workerContext->moveToThread(&runtime_->thread);
    connect(
        &runtime_->thread,
        &QThread::finished,
        runtime_->workerContext,
        &QObject::deleteLater
    );
    runtime_->thread.setObjectName(QStringLiteral("audioPlaybackThread"));
    runtime_->thread.start();
}

AudioPlaybackEngine::~AudioPlaybackEngine()
{
    stop();
}

void AudioPlaybackEngine::selectStream(StreamId streamId)
{
    if (streamId == kInvalidStreamId) {
        clearSelection();
        return;
    }
    const StreamId previous = runtime_->selected.exchange(streamId);
    runtime_->muted.store(false, std::memory_order_release);
    {
        const std::lock_guard<std::mutex> lock(runtime_->mutex);
        runtime_->packets.clear();
        runtime_->queuedBytes = 0;
        runtime_->metrics = {};
        runtime_->outputLatencySamples.clear();
        runtime_->sinkAudioStartedUs = -1;
        runtime_->sinkUnderrunStartedUs = -1;
        runtime_->sinkUnderrunAccumulatedUs = 0;
        runtime_->metrics.streamId = streamId;
        runtime_->metrics.requestedSinkBufferMs = kRequestedBufferMs;
    }
    QMetaObject::invokeMethod(
        runtime_->workerContext,
        [runtime = runtime_.get()] {
            runtime->resetDecoder();
            runtime->publishState(
                runtime->selected.load(), AudioPlaybackState::Buffering
            );
        },
        Qt::QueuedConnection
    );
    if (previous != kInvalidStreamId && previous != streamId) {
        emit stateChanged(previous, AudioPlaybackState::Muted);
    }
}

void AudioPlaybackEngine::clearSelection()
{
    const StreamId previous = runtime_->selected.exchange(kInvalidStreamId);
    runtime_->muted.store(true, std::memory_order_release);
    {
        const std::lock_guard<std::mutex> lock(runtime_->mutex);
        runtime_->packets.clear();
        runtime_->queuedBytes = 0;
    }
    QMetaObject::invokeMethod(
        runtime_->workerContext,
        [runtime = runtime_.get()] { runtime->resetDecoder(); },
        Qt::QueuedConnection
    );
    if (previous != kInvalidStreamId) {
        emit stateChanged(previous, AudioPlaybackState::Muted);
    }
}

void AudioPlaybackEngine::setMuted(bool muted)
{
    runtime_->muted.store(muted, std::memory_order_release);
    QMetaObject::invokeMethod(
        runtime_->workerContext,
        [runtime = runtime_.get(), muted] {
            if (runtime->sink != nullptr) runtime->sink->setVolume(muted ? 0.0 : 1.0);
            const StreamId selected = runtime->selected.load();
            if (selected != kInvalidStreamId) {
                runtime->publishState(
                    selected,
                    muted ? AudioPlaybackState::Muted
                          : AudioPlaybackState::Buffering
                );
            }
        },
        Qt::QueuedConnection
    );
}

StreamId AudioPlaybackEngine::selectedStream() const noexcept
{
    return runtime_->selected.load(std::memory_order_acquire);
}

bool AudioPlaybackEngine::isMuted() const noexcept
{
    return runtime_->muted.load(std::memory_order_acquire);
}

AudioPlaybackState AudioPlaybackEngine::state(StreamId streamId) const
{
    const std::lock_guard<std::mutex> lock(runtime_->mutex);
    const auto found = runtime_->states.find(streamId);
    return found != runtime_->states.end()
               ? found->second
               : AudioPlaybackState::Unavailable;
}

AudioPlaybackMetrics AudioPlaybackEngine::metricsSnapshot() const
{
    const std::lock_guard<std::mutex> lock(runtime_->mutex);
    AudioPlaybackMetrics result = runtime_->metrics;
    result.queuedPackets = static_cast<int>(runtime_->packets.size());
    result.queuedBytes = runtime_->queuedBytes;
    const qint64 activeUnderrunUs = runtime_->sinkUnderrunStartedUs >= 0
        ? monotonicMicroseconds() - runtime_->sinkUnderrunStartedUs : 0;
    result.underrunDurationMs =
        (runtime_->sinkUnderrunAccumulatedUs + activeUnderrunUs) / 1000;
    return result;
}

void AudioPlaybackEngine::setQualificationObserver(
    std::weak_ptr<AudioPlaybackObserver> observer
)
{
    const std::lock_guard<std::mutex> lock(runtime_->mutex);
    runtime_->qualificationObserver = std::move(observer);
}

void AudioPlaybackEngine::stop()
{
    if (!runtime_ || runtime_->stopping.exchange(true)) return;
    {
        const std::lock_guard<std::mutex> lock(runtime_->mutex);
        runtime_->packets.clear();
        runtime_->queuedBytes = 0;
    }
    if (runtime_->thread.isRunning()) {
        QMetaObject::invokeMethod(
            runtime_->workerContext,
            [runtime = runtime_.get()] { runtime->resetDecoder(); },
            Qt::BlockingQueuedConnection
        );
        runtime_->thread.quit();
        runtime_->thread.wait();
    }
}

void AudioPlaybackEngine::setVideoClockSource(
    StreamId streamId,
    std::shared_ptr<LatestFrameMailbox> mailbox
)
{
    const std::lock_guard<std::mutex> lock(runtime_->mutex);
    if (mailbox != nullptr) {
        runtime_->videoClocks[streamId] = std::move(mailbox);
    } else {
        runtime_->videoClocks.erase(streamId);
    }
}

void AudioPlaybackEngine::submitAudioConfiguration(
    StreamId streamId,
    const std::shared_ptr<void> &configurationValue,
    std::uint64_t sessionId
)
{
    const auto configuration =
        std::static_pointer_cast<FfmpegCodecConfiguration>(configurationValue);
    const bool supported = configuration != nullptr &&
        configuration->parameters != nullptr &&
        configuration->parameters->codec_id == AV_CODEC_ID_AAC;
    const AudioPlaybackState supportedState =
        runtime_->selected.load() == streamId && !runtime_->muted.load()
            ? AudioPlaybackState::Buffering
            : AudioPlaybackState::Muted;
    bool accepted = true;
    {
        const std::lock_guard<std::mutex> lock(runtime_->mutex);
        const auto currentSession =
            runtime_->configurationSessions.find(streamId);
        if (currentSession != runtime_->configurationSessions.end() &&
            currentSession->second > sessionId) {
            accepted = false;
        } else if (supported) {
            runtime_->configurations[streamId] = configuration;
            runtime_->configurationSessions[streamId] = sessionId;
            runtime_->states[streamId] = supportedState;
        } else {
            runtime_->configurations.erase(streamId);
            runtime_->configurationSessions.erase(streamId);
            runtime_->states[streamId] = AudioPlaybackState::Unavailable;
        }
    }
    if (!accepted) return;
    QMetaObject::invokeMethod(
        this,
        [this, streamId, supported, supportedState] {
            emit stateChanged(
                streamId,
                supported ? supportedState
                          : AudioPlaybackState::Unavailable
            );
        },
        Qt::QueuedConnection
    );
}

void AudioPlaybackEngine::submitAudioPacket(
    StreamId streamId,
    void *packetPointer,
    qint64 receivedMonotonicMs,
    std::uint64_t sessionId
)
{
    AVPacket *packet = static_cast<AVPacket *>(packetPointer);
    if (packet == nullptr) return;
    if (runtime_->stopping.load(std::memory_order_acquire) ||
        runtime_->selected.load(std::memory_order_acquire) != streamId) {
        av_packet_free(&packet);
        return;
    }

    auto owned = std::make_shared<OwnedPacket>();
    owned->packet = packet;
    owned->streamId = streamId;
    owned->receivedMonotonicMs = receivedMonotonicMs;
    owned->sessionId = sessionId;
    {
        const std::lock_guard<std::mutex> lock(runtime_->mutex);
        ++runtime_->metrics.packetsReceived;
        if (packet->size > kMaximumPacketBytes) {
            ++runtime_->metrics.packetsDropped;
            return;
        }
        while (!runtime_->packets.empty() &&
               (runtime_->packets.size() >= kMaximumPackets ||
                runtime_->queuedBytes + std::max(packet->size, 0) >
                    kMaximumPacketBytes)) {
            runtime_->queuedBytes -=
                std::max(runtime_->packets.front()->packet->size, 0);
            runtime_->packets.pop_front();
            ++runtime_->metrics.packetsDropped;
        }
        runtime_->queuedBytes += std::max(packet->size, 0);
        runtime_->packets.push_back(std::move(owned));
    }
    if (!runtime_->drainPosted.exchange(true, std::memory_order_acq_rel)) {
        QMetaObject::invokeMethod(
            runtime_->workerContext,
            [runtime = runtime_.get()] { runtime->drain(); },
            Qt::QueuedConnection
        );
    }
}

void AudioPlaybackEngine::invalidateAudioSession(
    StreamId streamId,
    std::uint64_t sessionId
)
{
    bool invalidated = false;
    {
        const std::lock_guard<std::mutex> lock(runtime_->mutex);
        const auto found = runtime_->configurationSessions.find(streamId);
        if (found != runtime_->configurationSessions.end() &&
            found->second <= sessionId) {
            runtime_->configurations.erase(streamId);
            runtime_->configurationSessions.erase(found);
            runtime_->states[streamId] = AudioPlaybackState::Unavailable;
            invalidated = true;
        }
        for (auto packet = runtime_->packets.begin();
             packet != runtime_->packets.end();) {
            if ((*packet)->streamId == streamId &&
                (*packet)->sessionId <= sessionId) {
                runtime_->queuedBytes -= std::max((*packet)->packet->size, 0);
                packet = runtime_->packets.erase(packet);
                ++runtime_->metrics.packetsDropped;
                invalidated = true;
            } else {
                ++packet;
            }
        }
    }
    if (runtime_->selected.load() == streamId && invalidated) {
        QMetaObject::invokeMethod(
            runtime_->workerContext,
            [runtime = runtime_.get(), streamId, sessionId] {
                if (runtime->decoderStream == streamId &&
                    runtime->decoderSession <= sessionId) {
                    runtime->resetDecoder();
                }
            },
            Qt::QueuedConnection
        );
    }
    if (invalidated) {
        QMetaObject::invokeMethod(
            this,
            [this, streamId] {
                emit stateChanged(streamId, AudioPlaybackState::Unavailable);
            },
            Qt::QueuedConnection
        );
    }
}
