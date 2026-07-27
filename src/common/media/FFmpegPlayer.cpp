#include "media/FFmpegPlayer.h"

#include <QByteArray>
#include <QDateTime>
#include <QMetaObject>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace {

constexpr int kNetworkTimeoutMicroseconds = 3'000'000;
constexpr std::array<int, 4> kReconnectDelaysMs {1'000, 2'000, 4'000, 5'000};
constexpr int kDecodeBatchPackets = 4;
constexpr qint64 kDecodeBatchMilliseconds = 5;
constexpr std::size_t kMaximumLatencySamples = 20'000;

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

class FFmpegNetworkRuntime final
{
public:
    FFmpegNetworkRuntime()
        : initialized_(avformat_network_init() >= 0)
    {
    }

    ~FFmpegNetworkRuntime()
    {
        if (initialized_) {
            avformat_network_deinit();
        }
    }

    [[nodiscard]] bool isInitialized() const noexcept
    {
        return initialized_;
    }

private:
    bool initialized_ = false;
};

FFmpegNetworkRuntime &ffmpegNetworkRuntime()
{
    static FFmpegNetworkRuntime runtime;
    return runtime;
}

struct FormatContextHandle
{
    AVFormatContext *value = nullptr;

    ~FormatContextHandle()
    {
        if (value != nullptr) {
            avformat_close_input(&value);
        }
    }
};

struct DictionaryHandle
{
    AVDictionary *value = nullptr;

    ~DictionaryHandle()
    {
        av_dict_free(&value);
    }
};

struct CodecConfiguration
{
    AVCodecParameters *parameters = avcodec_parameters_alloc();
    AVRational timeBase {0, 1};

    ~CodecConfiguration()
    {
        avcodec_parameters_free(&parameters);
    }
};

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
    SwsContext *swsContext = nullptr;
    int outputWidth = 0;
    int outputHeight = 0;
    qint64 lastConvertedMs = 0;
    std::uint64_t sessionId = 0;

    ~DecoderRuntime()
    {
        reset();
    }

    void reset()
    {
        if (swsContext != nullptr) {
            sws_freeContext(swsContext);
            swsContext = nullptr;
        }
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (codecContext != nullptr) {
            avcodec_free_context(&codecContext);
        }
        outputWidth = 0;
        outputHeight = 0;
        lastConvertedMs = 0;
        sessionId = 0;
    }
};

std::uint8_t markerCrc(std::uint32_t value)
{
    std::uint8_t crc = 0;
    for (int byteIndex = 3; byteIndex >= 0; --byteIndex) {
        crc ^= static_cast<std::uint8_t>((value >> (byteIndex * 8)) & 0xffU);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) != 0U
                      ? static_cast<std::uint8_t>((crc << 1U) ^ 0x07U)
                      : static_cast<std::uint8_t>(crc << 1U);
        }
    }
    return crc;
}

std::optional<qint64> decodeLatencyMarker(const QImage &image)
{
    if (image.isNull() || image.width() < 240 || image.height() < 120) {
        return std::nullopt;
    }

    constexpr double referenceWidth = 1280.0;
    constexpr double referenceHeight = 720.0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    auto decodeGeometry =
        [&image, now](double startX, double cellPitch, double centerOffset)
        -> std::optional<qint64> {
        constexpr double sampleY = 30.0;
        const int y = std::clamp(
            qRound((sampleY / referenceHeight) * image.height()),
            0,
            image.height() - 1
        );
        auto sampleBit = [&image, y, startX, cellPitch, centerOffset](
                             int bitIndex
                         ) {
            const int x = std::clamp(
                qRound(
                    ((startX + bitIndex * cellPitch + centerOffset) /
                     referenceWidth) *
                    image.width()
                ),
                0,
                image.width() - 1
            );
            return qGray(image.pixelColor(x, y).rgb()) >= 160;
        };

        std::uint32_t timestamp = 0;
        for (int bit = 0; bit < 32; ++bit) {
            timestamp = static_cast<std::uint32_t>(
                (timestamp << 1U) | (sampleBit(bit) ? 1U : 0U)
            );
        }

        std::uint8_t expectedCrc = 0;
        for (int bit = 32; bit < 40; ++bit) {
            expectedCrc = static_cast<std::uint8_t>(
                (expectedCrc << 1U) | (sampleBit(bit) ? 1U : 0U)
            );
        }
        if (markerCrc(timestamp) != expectedCrc) {
            return std::nullopt;
        }

        const std::uint32_t nowLow = static_cast<std::uint32_t>(now);
        const std::uint32_t elapsed = nowLow - timestamp;
        if (elapsed > 10'000U) {
            return std::nullopt;
        }
        return now - static_cast<qint64>(elapsed);
    };

    // Mixed-DPI WPF layout can round the 28 px design pitch to a 29 px pitch
    // in the 1280x720 captured frame. Prefer the two expected layouts, then
    // perform a small bounded search. CRC plus the ten-second timestamp window
    // prevents accepting an unrelated bright/dark pattern.
    for (const auto &geometry :
         {std::array<double, 3> {20.0, 29.0, 13.0},
          std::array<double, 3> {20.0, 28.0, 13.0}}) {
        if (const auto decoded =
                decodeGeometry(geometry[0], geometry[1], geometry[2]);
            decoded.has_value()) {
            return decoded;
        }
    }
    for (int pitch = 20; pitch <= 32; ++pitch) {
        for (int start = 12; start <= 24; ++start) {
            if (const auto decoded =
                    decodeGeometry(start, pitch, pitch / 2.0);
                decoded.has_value()) {
                return decoded;
            }
        }
    }
    return std::nullopt;
}

qint64 percentile(std::vector<qint64> values, double fraction)
{
    if (values.empty()) {
        return -1;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::clamp(
            std::ceil(fraction * static_cast<double>(values.size())) - 1.0,
            0.0,
            static_cast<double>(values.size() - 1)
        )
    );
    return values.at(index);
}

QString stateName(FFmpegPlayer::PlaybackState state)
{
    switch (state) {
    case FFmpegPlayer::PlaybackState::Stopped:
        return QStringLiteral("stopped");
    case FFmpegPlayer::PlaybackState::Connecting:
        return QStringLiteral("connecting");
    case FFmpegPlayer::PlaybackState::Playing:
        return QStringLiteral("playing");
    case FFmpegPlayer::PlaybackState::Reconnecting:
        return QStringLiteral("reconnecting");
    }
    return QStringLiteral("unknown");
}

int defaultDecodeWorkerCount()
{
    const int ideal = QThread::idealThreadCount();
    return std::clamp(ideal > 0 ? ideal / 2 : 1, 1, 8);
}

} // namespace

struct FFmpegPlayer::SharedState
{
    FFmpegPlayer *owner = nullptr;
    DecodeWorkerPool *pool = nullptr;
    int workerIndex = 0;
    PlaybackPerformanceOptions options;

    std::mutex queueMutex;
    std::condition_variable idleCondition;
    std::deque<std::shared_ptr<EncodedPacket>> packets;
    qint64 queueBytes = 0;
    std::shared_ptr<CodecConfiguration> codecConfiguration;
    std::uint64_t configurationSessionId = 0;
    bool decoderResetRequired = false;
    bool dropUntilKeyframe = false;
    bool decodeScheduled = false;
    bool stopping = false;
    DecoderRuntime decoder;

    std::mutex frameMutex;
    PresentableVideoFrame latestFrame;
    std::atomic_uint64_t frameSequence {0};
    std::atomic_bool automaticFrameSignals {true};
    std::atomic_bool automaticDeliveryScheduled {false};

    std::mutex presentationMutex;
    PresentationTarget presentationTarget;

    std::atomic_uint64_t packetsReceived {0};
    std::atomic_uint64_t packetBytesReceived {0};
    std::atomic_uint64_t packetsDropped {0};
    std::atomic_uint64_t decodedFrames {0};
    std::atomic_uint64_t convertedFrames {0};
    std::atomic_uint64_t presentedFrames {0};
    std::atomic_uint64_t reconnectCount {0};
    std::atomic_uint64_t playingSessionId {0};
    std::atomic<qint64> lastFrameMonotonicMs {-1};

    std::mutex latencyMutex;
    std::vector<qint64> internalLatencySamples;
    std::vector<qint64> sourceLatencySamples;
};

FFmpegPlayer::FFmpegPlayer(QObject *parent)
    : QObject(parent)
{
    options_.decodeWorkerCount = 1;
    sharedState_ = std::make_shared<SharedState>();
    sharedState_->owner = this;
    sharedState_->workerIndex = 0;
    sharedState_->options = options_;
    qRegisterMetaType<FFmpegPlayer::PlaybackState>();
    qRegisterMetaType<PresentableVideoFrame>();
    (void)ffmpegNetworkRuntime();
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

    sharedState_ = std::make_shared<SharedState>();
    sharedState_->owner = this;
    sharedState_->pool = decodeWorkerPool_;
    sharedState_->workerIndex = decodeWorkerPool_->workerIndexFor(streamId_);
    sharedState_->options = options_;
    qRegisterMetaType<FFmpegPlayer::PlaybackState>();
    qRegisterMetaType<PresentableVideoFrame>();
    (void)ffmpegNetworkRuntime();
}

FFmpegPlayer::~FFmpegPlayer()
{
    stop();
    sharedState_->owner = nullptr;
}

bool FFmpegPlayer::start(const QString &rtmpUrl)
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (isRunning()) {
        emit errorOccurred(tr("播放器已经在运行。"));
        return false;
    }
    if (!ffmpegNetworkRuntime().isInitialized()) {
        emit errorOccurred(tr("FFmpeg 网络模块初始化失败。"));
        return false;
    }

    const QUrl parsedUrl(rtmpUrl, QUrl::StrictMode);
    if (!parsedUrl.isValid() ||
        parsedUrl.scheme().compare(QStringLiteral("rtmp"), Qt::CaseInsensitive) != 0 ||
        parsedUrl.host().isEmpty() || parsedUrl.path().isEmpty()) {
        emit errorOccurred(tr("RTMP URL 无效；仅支持 rtmp:// 地址。"));
        setStateOnOwnerThread(PlaybackState::Stopped);
        return false;
    }

    if (decodeWorkerPool_ == nullptr) {
        ownedDecodeWorkerPool_ = std::make_unique<DecodeWorkerPool>(1);
        decodeWorkerPool_ = ownedDecodeWorkerPool_.get();
        sharedState_->pool = decodeWorkerPool_;
        sharedState_->workerIndex = 0;
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
        const std::lock_guard<std::mutex> lock(sharedState_->queueMutex);
        sharedState_->packets.clear();
        sharedState_->queueBytes = 0;
        sharedState_->codecConfiguration.reset();
        sharedState_->configurationSessionId = 0;
        sharedState_->decoderResetRequired = true;
        sharedState_->dropUntilKeyframe = false;
        sharedState_->stopping = false;
    }
    {
        const std::lock_guard<std::mutex> lock(sharedState_->frameMutex);
        sharedState_->latestFrame = {};
    }
    sharedState_->automaticDeliveryScheduled.store(false, std::memory_order_release);

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
        state_ == PlaybackState::Stopped) {
        return;
    }
    requestStop();

    if (networkThread_ != nullptr) {
        networkThread_->wait();
        networkThread_.reset();
    }

    {
        std::unique_lock<std::mutex> lock(sharedState_->queueMutex);
        sharedState_->stopping = true;
        sharedState_->packets.clear();
        sharedState_->queueBytes = 0;
        sharedState_->decoderResetRequired = true;
        if (sharedState_->decodeScheduled) {
            sharedState_->idleCondition.wait(lock, [this] {
                return !sharedState_->decodeScheduled;
            });
        } else {
            // 没有 worker 正在访问解码上下文时可直接释放；不要为了空播放器
            // 额外投递清理任务，避免析构阶段创建无意义的异步工作。
            sharedState_->decoder.reset();
        }
        sharedState_->codecConfiguration.reset();
        sharedState_->stopping = false;
    }

    sessionId_.fetch_add(1, std::memory_order_acq_rel);
    {
        const std::lock_guard<std::mutex> lock(sharedState_->frameMutex);
        sharedState_->latestFrame = {};
    }
    setStateOnOwnerThread(PlaybackState::Stopped);
}

bool FFmpegPlayer::isRunning() const noexcept
{
    Q_ASSERT(QThread::currentThread() == thread());
    return networkThread_ != nullptr && networkThread_->isRunning();
}

void FFmpegPlayer::setAutomaticFrameSignalsEnabled(bool enabled) noexcept
{
    sharedState_->automaticFrameSignals.store(enabled, std::memory_order_release);
}

void FFmpegPlayer::setPresentationTarget(const PresentationTarget &target)
{
    const std::lock_guard<std::mutex> lock(sharedState_->presentationMutex);
    sharedState_->presentationTarget = target;
}

PresentableVideoFrame FFmpegPlayer::latestFrame() const
{
    const std::lock_guard<std::mutex> lock(sharedState_->frameMutex);
    return sharedState_->latestFrame;
}

void FFmpegPlayer::markFramePresented(const PresentableVideoFrame &frame)
{
    if (frame.image.isNull()) {
        return;
    }

    sharedState_->presentedFrames.fetch_add(1, std::memory_order_relaxed);
    const qint64 nowMonotonic = monotonicMilliseconds();
    const qint64 internalLatency =
        std::max<qint64>(0, nowMonotonic - frame.receivedMonotonicMs);

    const std::lock_guard<std::mutex> lock(sharedState_->latencyMutex);
    auto appendSample = [](std::vector<qint64> &samples, qint64 value) {
        if (samples.size() >= kMaximumLatencySamples) {
            samples.erase(samples.begin(), samples.begin() + samples.size() / 4);
        }
        samples.push_back(value);
    };
    appendSample(sharedState_->internalLatencySamples, internalLatency);
    if (frame.sourceTimestampMs >= 0) {
        const qint64 sourceLatency =
            QDateTime::currentMSecsSinceEpoch() - frame.sourceTimestampMs;
        if (sourceLatency >= 0 && sourceLatency <= 10'000) {
            appendSample(sharedState_->sourceLatencySamples, sourceLatency);
        }
    }
}

StreamMetrics FFmpegPlayer::metricsSnapshot()
{
    Q_ASSERT(QThread::currentThread() == thread());

    const qint64 now = monotonicMilliseconds();
    const std::uint64_t decoded =
        sharedState_->decodedFrames.load(std::memory_order_relaxed);
    const std::uint64_t presented =
        sharedState_->presentedFrames.load(std::memory_order_relaxed);
    if (lastMetricsSampleMs_ > 0 && now > lastMetricsSampleMs_) {
        const double seconds = static_cast<double>(now - lastMetricsSampleMs_) / 1000.0;
        decodeFps_ = static_cast<double>(decoded - lastDecodedSample_) / seconds;
        displayFps_ = static_cast<double>(presented - lastPresentedSample_) / seconds;
    }
    lastMetricsSampleMs_ = now;
    lastDecodedSample_ = decoded;
    lastPresentedSample_ = presented;

    StreamMetrics metrics;
    metrics.streamId = streamId_;
    metrics.displayName = displayName_;
    metrics.state = stateName(state_);
    metrics.packetsReceived =
        sharedState_->packetsReceived.load(std::memory_order_relaxed);
    metrics.packetBytesReceived =
        sharedState_->packetBytesReceived.load(std::memory_order_relaxed);
    metrics.packetsDropped =
        sharedState_->packetsDropped.load(std::memory_order_relaxed);
    metrics.decodedFrames = decoded;
    metrics.convertedFrames =
        sharedState_->convertedFrames.load(std::memory_order_relaxed);
    metrics.presentedFrames = presented;
    metrics.reconnectCount =
        sharedState_->reconnectCount.load(std::memory_order_relaxed);
    metrics.decodeFps = decodeFps_;
    metrics.displayFps = displayFps_;

    {
        const std::lock_guard<std::mutex> lock(sharedState_->queueMutex);
        metrics.queuePackets = static_cast<int>(sharedState_->packets.size());
        metrics.queueBytes = sharedState_->queueBytes;
    }
    const qint64 lastFrame =
        sharedState_->lastFrameMonotonicMs.load(std::memory_order_relaxed);
    metrics.lastFrameAgeMs = lastFrame >= 0 ? std::max<qint64>(0, now - lastFrame) : -1;

    {
        const std::lock_guard<std::mutex> lock(sharedState_->latencyMutex);
        metrics.internalLatencyP95Ms =
            percentile(sharedState_->internalLatencySamples, 0.95);
        metrics.sourceLatencyP50Ms =
            percentile(sharedState_->sourceLatencySamples, 0.50);
        metrics.sourceLatencyP95Ms =
            percentile(sharedState_->sourceLatencySamples, 0.95);
        metrics.sourceLatencyMaxMs =
            sharedState_->sourceLatencySamples.empty()
                ? -1
                : *std::max_element(
                      sharedState_->sourceLatencySamples.begin(),
                      sharedState_->sourceLatencySamples.end()
                  );
        metrics.sourceLatencySamples = sharedState_->sourceLatencySamples.size();
    }
    return metrics;
}

void FFmpegPlayer::decodeNetworkLoop(QString rtmpUrl, std::uint64_t sessionId)
{
    const QByteArray encodedUrl = rtmpUrl.toUtf8();
    int reconnectDelayIndex = 0;
    bool firstAttempt = true;

    while (!stopRequested_.load(std::memory_order_acquire)) {
        postState(
            firstAttempt ? PlaybackState::Connecting : PlaybackState::Reconnecting,
            sessionId
        );
        if (!firstAttempt) {
            sharedState_->reconnectCount.fetch_add(1, std::memory_order_relaxed);
        }
        restartRequested_.store(false, std::memory_order_release);

        QString errorMessage;
        bool receivedPackets = false;
        {
            FormatContextHandle formatContext;
            formatContext.value = avformat_alloc_context();
            if (formatContext.value == nullptr) {
                errorMessage = tr("无法分配 FFmpeg 输入上下文。");
            } else {
                formatContext.value->interrupt_callback = {
                    &FFmpegPlayer::interruptCallback, this
                };

                DictionaryHandle inputOptions;
                const QByteArray timeout =
                    QByteArray::number(kNetworkTimeoutMicroseconds);
                av_dict_set(&inputOptions.value, "rtmp_live", "live", 0);
                av_dict_set(&inputOptions.value, "rw_timeout", timeout.constData(), 0);
                av_dict_set(&inputOptions.value, "fflags", "nobuffer", 0);
                av_dict_set(&inputOptions.value, "probesize", "32768", 0);
                av_dict_set(&inputOptions.value, "analyzeduration", "1000000", 0);

                int status = avformat_open_input(
                    &formatContext.value,
                    encodedUrl.constData(),
                    nullptr,
                    &inputOptions.value
                );
                if (status < 0) {
                    if (!stopRequested_.load(std::memory_order_acquire)) {
                        errorMessage =
                            tr("打开 RTMP 输入失败：%1").arg(ffmpegError(status));
                    }
                } else {
                    status = avformat_find_stream_info(formatContext.value, nullptr);
                    if (status < 0) {
                        errorMessage =
                            tr("读取流信息失败：%1").arg(ffmpegError(status));
                    } else {
                        const AVCodec *decoder = nullptr;
                        const int videoStreamIndex = av_find_best_stream(
                            formatContext.value,
                            AVMEDIA_TYPE_VIDEO,
                            -1,
                            -1,
                            &decoder,
                            0
                        );
                        if (videoStreamIndex < 0 || decoder == nullptr) {
                            errorMessage = tr("RTMP 输入中没有可解码的视频流。");
                        } else {
                            const AVCodecParameters *parameters =
                                formatContext.value->streams[videoStreamIndex]->codecpar;
                            if (parameters->codec_id != AV_CODEC_ID_H264) {
                                errorMessage = tr("当前版本只支持 H.264 视频流。");
                            } else {
                                auto configuration =
                                    std::make_shared<CodecConfiguration>();
                                if (configuration->parameters == nullptr ||
                                    avcodec_parameters_copy(
                                        configuration->parameters, parameters
                                    ) < 0) {
                                    errorMessage = tr("复制视频流参数失败。");
                                } else {
                                    configuration->timeBase =
                                        formatContext.value->streams[
                                            videoStreamIndex
                                        ]->time_base;
                                    enqueueDecoderConfiguration(
                                        std::static_pointer_cast<void>(configuration),
                                        sessionId
                                    );

                                    AVPacket *packet = av_packet_alloc();
                                    if (packet == nullptr) {
                                        errorMessage = tr("无法分配视频数据包。");
                                    } else {
                                        while (!stopRequested_.load(
                                                   std::memory_order_acquire
                                               ) &&
                                               !restartRequested_.load(
                                                   std::memory_order_acquire
                                               )) {
                                            status = av_read_frame(
                                                formatContext.value, packet
                                            );
                                            if (status < 0) {
                                                if (!stopRequested_.load(
                                                        std::memory_order_acquire
                                                    ) &&
                                                    !restartRequested_.load(
                                                        std::memory_order_acquire
                                                    )) {
                                                    errorMessage = tr(
                                                        "视频流已中断：%1"
                                                    ).arg(ffmpegError(status));
                                                }
                                                break;
                                            }
                                            if (packet->stream_index !=
                                                videoStreamIndex) {
                                                av_packet_unref(packet);
                                                continue;
                                            }

                                            AVPacket *ownedPacket =
                                                av_packet_alloc();
                                            if (ownedPacket == nullptr) {
                                                av_packet_unref(packet);
                                                errorMessage =
                                                    tr("无法复制视频数据包。");
                                                break;
                                            }
                                            av_packet_move_ref(ownedPacket, packet);
                                            receivedPackets = true;
                                            enqueuePacket(
                                                ownedPacket,
                                                monotonicMilliseconds(),
                                                sessionId
                                            );
                                        }
                                        av_packet_free(&packet);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }
        if (receivedPackets) {
            reconnectDelayIndex = 0;
        }
        const int reconnectDelayMs = kReconnectDelaysMs.at(reconnectDelayIndex);
        if (!receivedPackets) {
            reconnectDelayIndex = std::min(
                reconnectDelayIndex + 1,
                static_cast<int>(kReconnectDelaysMs.size()) - 1
            );
        }
        postState(PlaybackState::Reconnecting, sessionId);
        if (!errorMessage.isEmpty()) {
            postError(
                tr("%1；%2 秒后重试。")
                    .arg(errorMessage)
                    .arg(reconnectDelayMs / 1000),
                sessionId
            );
        }
        firstAttempt = false;
        if (!waitForReconnect(reconnectDelayMs)) {
            break;
        }
    }

    postState(PlaybackState::Stopped, sessionId);
}

void FFmpegPlayer::enqueueDecoderConfiguration(
    const std::shared_ptr<void> &codecConfiguration,
    std::uint64_t sessionId
)
{
    const auto configuration =
        std::static_pointer_cast<CodecConfiguration>(codecConfiguration);
    const std::lock_guard<std::mutex> lock(sharedState_->queueMutex);
    if (sharedState_->stopping ||
        sessionId != sessionId_.load(std::memory_order_acquire)) {
        return;
    }

    sharedState_->packetsDropped.fetch_add(
        sharedState_->packets.size(), std::memory_order_relaxed
    );
    sharedState_->packets.clear();
    sharedState_->queueBytes = 0;
    sharedState_->codecConfiguration = configuration;
    sharedState_->configurationSessionId = sessionId;
    sharedState_->decoderResetRequired = true;
    sharedState_->dropUntilKeyframe = false;
    sharedState_->playingSessionId.store(0, std::memory_order_release);
    scheduleDecodeLocked(sharedState_);
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
    const std::lock_guard<std::mutex> lock(sharedState_->queueMutex);
    if (sharedState_->stopping ||
        sessionId != sessionId_.load(std::memory_order_acquire)) {
        return;
    }

    sharedState_->packetsReceived.fetch_add(1, std::memory_order_relaxed);
    sharedState_->packetBytesReceived.fetch_add(
        static_cast<std::uint64_t>(packetSize), std::memory_order_relaxed
    );

    if (sharedState_->dropUntilKeyframe && !keyFrame) {
        sharedState_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const bool wouldOverflow =
        static_cast<int>(sharedState_->packets.size()) >=
            sharedState_->options.maximumQueuedPackets ||
        sharedState_->queueBytes + packetSize >
            sharedState_->options.maximumQueuedBytes;
    if (wouldOverflow) {
        sharedState_->packetsDropped.fetch_add(
            sharedState_->packets.size(), std::memory_order_relaxed
        );
        sharedState_->packets.clear();
        sharedState_->queueBytes = 0;
        sharedState_->decoderResetRequired = true;
        sharedState_->dropUntilKeyframe = !keyFrame;
        if (!keyFrame) {
            sharedState_->packetsDropped.fetch_add(1, std::memory_order_relaxed);
            scheduleDecodeLocked(sharedState_);
            return;
        }
    }

    sharedState_->dropUntilKeyframe = false;
    sharedState_->queueBytes += packetSize;
    sharedState_->packets.push_back(std::move(encodedPacket));
    scheduleDecodeLocked(sharedState_);
}

void FFmpegPlayer::scheduleDecodeLocked(
    const std::shared_ptr<SharedState> &state
)
{
    if (state->decodeScheduled) {
        return;
    }
    state->decodeScheduled = true;
    if (!state->pool->post(state->workerIndex, [state] {
            drainDecodeState(state);
        })) {
        state->decodeScheduled = false;
        state->idleCondition.notify_all();
    }
}

void FFmpegPlayer::drainDecodeState(
    const std::shared_ptr<SharedState> &state
)
{
    const qint64 batchStarted = monotonicMilliseconds();
    QString decodeError;

    bool resetDecoder = false;
    bool stopping = false;
    std::shared_ptr<CodecConfiguration> configuration;
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
            [&state, &decodeError](qint64 receivedMonotonicMs) {
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

                    PresentationTarget target;
                    {
                        const std::lock_guard<std::mutex> lock(
                            state->presentationMutex
                        );
                        target = state->presentationTarget;
                    }
                    const int maximumFps = std::max(
                        1,
                        target.fullscreen
                            ? state->options.fullscreenFps
                            : state->options.gridFps
                    );
                    const qint64 intervalMs = std::max<qint64>(
                        1, 1000 / maximumFps
                    );
                    if (now - state->decoder.lastConvertedMs < intervalMs) {
                        av_frame_unref(frame);
                        continue;
                    }
                    state->decoder.lastConvertedMs = now;

                    QSize maximumSize = target.fullscreen
                                            ? state->options.fullscreenMaximumSize
                                            : state->options.gridMaximumSize;
                    QSize viewport = target.viewportSize.isValid()
                                         ? target.viewportSize
                                         : maximumSize;
                    viewport = viewport.boundedTo(maximumSize);
                    QSize outputSize(frame->width, frame->height);
                    outputSize.scale(viewport, Qt::KeepAspectRatio);
                    outputSize.setWidth(std::min(outputSize.width(), frame->width));
                    outputSize.setHeight(
                        std::min(outputSize.height(), frame->height)
                    );
                    if (!outputSize.isValid()) {
                        av_frame_unref(frame);
                        continue;
                    }

                    state->decoder.swsContext = sws_getCachedContext(
                        state->decoder.swsContext,
                        frame->width,
                        frame->height,
                        static_cast<AVPixelFormat>(frame->format),
                        outputSize.width(),
                        outputSize.height(),
                        AV_PIX_FMT_RGB24,
                        SWS_BILINEAR,
                        nullptr,
                        nullptr,
                        nullptr
                    );
                    if (state->decoder.swsContext == nullptr) {
                        av_frame_unref(frame);
                        decodeError =
                            QObject::tr("创建 RGB24 转换上下文失败。");
                        return;
                    }

                    QImage image(
                        outputSize.width(),
                        outputSize.height(),
                        QImage::Format_RGB888
                    );
                    std::array<std::uint8_t *, 4> destinationData {
                        image.bits(), nullptr, nullptr, nullptr
                    };
                    std::array<int, 4> destinationLinesize {
                        static_cast<int>(image.bytesPerLine()), 0, 0, 0
                    };
                    const int convertedRows = sws_scale(
                        state->decoder.swsContext,
                        frame->data,
                        frame->linesize,
                        0,
                        frame->height,
                        destinationData.data(),
                        destinationLinesize.data()
                    );
                    av_frame_unref(frame);
                    if (convertedRows <= 0) {
                        decodeError = QObject::tr("YUV 到 RGB888 转换失败。");
                        return;
                    }

                    PresentableVideoFrame presentable;
                    presentable.image = std::move(image);
                    presentable.sequence =
                        state->frameSequence.fetch_add(
                            1, std::memory_order_relaxed
                        ) +
                        1;
                    presentable.receivedMonotonicMs = receivedMonotonicMs;
                    if (state->options.latencyMarkerEnabled) {
                        const auto sourceTimestamp =
                            decodeLatencyMarker(presentable.image);
                        if (sourceTimestamp.has_value()) {
                            presentable.sourceTimestampMs =
                                sourceTimestamp.value();
                        }
                    }
                    {
                        const std::lock_guard<std::mutex> lock(
                            state->frameMutex
                        );
                        state->latestFrame = presentable;
                    }
                    state->convertedFrames.fetch_add(
                        1, std::memory_order_relaxed
                    );
                    state->lastFrameMonotonicMs.store(
                        now, std::memory_order_relaxed
                    );

                    if (state->playingSessionId.exchange(
                            state->decoder.sessionId,
                            std::memory_order_acq_rel
                        ) != state->decoder.sessionId &&
                        state->owner != nullptr) {
                        state->owner->postState(
                            PlaybackState::Playing,
                            state->decoder.sessionId
                        );
                    }
                    if (state->automaticFrameSignals.load(
                            std::memory_order_acquire
                        ) &&
                        !state->automaticDeliveryScheduled.exchange(
                            true, std::memory_order_acq_rel
                        ) &&
                        state->owner != nullptr) {
                        FFmpegPlayer *owner = state->owner;
                        const std::uint64_t session =
                            state->decoder.sessionId;
                        QMetaObject::invokeMethod(
                            owner,
                            [owner, session] {
                                owner->deliverLatestFrame(session);
                            },
                            Qt::QueuedConnection
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
        owner->postError(decodeError, session);
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
                    drainDecodeState(state);
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

void FFmpegPlayer::deliverLatestFrame(std::uint64_t sessionId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    sharedState_->automaticDeliveryScheduled.store(
        false, std::memory_order_release
    );
    if (sessionId != sessionId_.load(std::memory_order_acquire)) {
        return;
    }

    const PresentableVideoFrame frame = latestFrame();
    if (frame.image.isNull() || frame.sequence == lastAutomaticSequence_) {
        return;
    }
    lastAutomaticSequence_ = frame.sequence;
    emit frameReady(frame.image);
    markFramePresented(frame);
}

void FFmpegPlayer::postState(
    PlaybackState state,
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

void FFmpegPlayer::postError(QString message, std::uint64_t sessionId)
{
    QMetaObject::invokeMethod(
        this,
        [this, message = std::move(message), sessionId] {
            if (sessionId == sessionId_.load(std::memory_order_acquire)) {
                emit errorOccurred(message);
            }
        },
        Qt::QueuedConnection
    );
}

void FFmpegPlayer::setStateOnOwnerThread(PlaybackState state)
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

int FFmpegPlayer::interruptCallback(void *opaque) noexcept
{
    const auto *player = static_cast<const FFmpegPlayer *>(opaque);
    return player->stopRequested_.load(std::memory_order_acquire) ||
                   player->restartRequested_.load(std::memory_order_acquire)
               ? 1
               : 0;
}
