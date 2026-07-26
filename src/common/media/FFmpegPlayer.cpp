#include "media/FFmpegPlayer.h"

#include <QByteArray>
#include <QMetaObject>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace {

constexpr int kNetworkTimeoutMicroseconds = 3'000'000;
constexpr std::array<int, 4> kReconnectDelaysMs {1'000, 2'000, 4'000, 5'000};

QString ffmpegError(int errorCode)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer {};
    if (av_strerror(errorCode, buffer.data(), buffer.size()) < 0) {
        return QStringLiteral("未知 FFmpeg 错误 (%1)").arg(errorCode);
    }
    return QString::fromUtf8(buffer.data());
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

struct CodecContextDeleter
{
    void operator()(AVCodecContext *context) const noexcept
    {
        avcodec_free_context(&context);
    }
};

struct PacketDeleter
{
    void operator()(AVPacket *packet) const noexcept
    {
        av_packet_free(&packet);
    }
};

struct FrameDeleter
{
    void operator()(AVFrame *frame) const noexcept
    {
        av_frame_free(&frame);
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

struct SwsContextHandle
{
    SwsContext *value = nullptr;

    ~SwsContextHandle()
    {
        sws_freeContext(value);
    }
};

using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

struct DecodeAttemptResult
{
    bool decodedFrame = false;
    QString errorMessage;
};

} // namespace

FFmpegPlayer::FFmpegPlayer(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<FFmpegPlayer::PlaybackState>();
}

FFmpegPlayer::~FFmpegPlayer()
{
    stop();
}

bool FFmpegPlayer::start(const QString &rtmpUrl)
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (isRunning()) {
        emit errorOccurred(tr("播放器已经在运行。"));
        return false;
    }

    if (decodeThread_ != nullptr) {
        decodeThread_->wait();
        decodeThread_.reset();
    }

    const QUrl parsedUrl(rtmpUrl, QUrl::StrictMode);
    if (!parsedUrl.isValid() || parsedUrl.scheme().compare(
            QStringLiteral("rtmp"), Qt::CaseInsensitive) != 0 ||
        parsedUrl.host().isEmpty() || parsedUrl.path().isEmpty()) {
        emit errorOccurred(tr("RTMP URL 无效；仅支持 rtmp:// 地址。"));
        setStateOnOwnerThread(PlaybackState::Stopped);
        return false;
    }

    stopRequested_.store(false, std::memory_order_release);
    const std::uint64_t newSessionId =
        sessionId_.fetch_add(1, std::memory_order_acq_rel) + 1;

    {
        const std::lock_guard<std::mutex> lock(frameMutex_);
        pendingFrame_ = QImage();
        frameDeliveryScheduled_ = false;
    }

    decodeThread_.reset(QThread::create(
        [this, rtmpUrl, newSessionId] {
            decodeLoop(rtmpUrl, newSessionId);
        }
    ));
    decodeThread_->setObjectName(QStringLiteral("FFmpegDecodeThread"));
    decodeThread_->start();
    return true;
}

void FFmpegPlayer::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());

    stopRequested_.store(true, std::memory_order_release);
    reconnectCondition_.notify_all();

    if (decodeThread_ != nullptr) {
        decodeThread_->wait();
        decodeThread_.reset();
    }

    sessionId_.fetch_add(1, std::memory_order_acq_rel);
    {
        const std::lock_guard<std::mutex> lock(frameMutex_);
        pendingFrame_ = QImage();
        frameDeliveryScheduled_ = false;
    }
    setStateOnOwnerThread(PlaybackState::Stopped);
}

bool FFmpegPlayer::isRunning() const noexcept
{
    Q_ASSERT(QThread::currentThread() == thread());
    return decodeThread_ != nullptr && decodeThread_->isRunning();
}

void FFmpegPlayer::decodeLoop(QString rtmpUrl, std::uint64_t sessionId)
{
    if (avformat_network_init() < 0) {
        postError(tr("FFmpeg 网络模块初始化失败。"), sessionId);
        postState(PlaybackState::Stopped, sessionId);
        return;
    }

    const QByteArray encodedUrl = rtmpUrl.toUtf8();
    int reconnectDelayIndex = 0;
    bool firstAttempt = true;

    const auto decodeAttempt = [this, &encodedUrl, sessionId]() -> DecodeAttemptResult {
        DecodeAttemptResult result;
        FormatContextHandle formatContext;
        formatContext.value = avformat_alloc_context();
        if (formatContext.value == nullptr) {
            result.errorMessage = tr("无法分配 FFmpeg 输入上下文。");
            return result;
        }
        formatContext.value->interrupt_callback = {&FFmpegPlayer::interruptCallback, this};

        DictionaryHandle inputOptions;
        const QByteArray timeout = QByteArray::number(kNetworkTimeoutMicroseconds);
        av_dict_set(&inputOptions.value, "rtmp_live", "live", 0);
        av_dict_set(&inputOptions.value, "rw_timeout", timeout.constData(), 0);
        av_dict_set(&inputOptions.value, "fflags", "nobuffer", 0);
        av_dict_set(&inputOptions.value, "probesize", "32768", 0);
        av_dict_set(&inputOptions.value, "analyzeduration", "1000000", 0);

        int status = avformat_open_input(
            &formatContext.value, encodedUrl.constData(), nullptr, &inputOptions.value
        );
        if (status < 0) {
            if (!stopRequested_.load(std::memory_order_acquire)) {
                result.errorMessage =
                    tr("打开 RTMP 输入失败：%1").arg(ffmpegError(status));
            }
            return result;
        }

        status = avformat_find_stream_info(formatContext.value, nullptr);
        if (status < 0) {
            if (!stopRequested_.load(std::memory_order_acquire)) {
                result.errorMessage =
                    tr("读取流信息失败：%1").arg(ffmpegError(status));
            }
            return result;
        }

        const AVCodec *decoder = nullptr;
        const int videoStreamIndex = av_find_best_stream(
            formatContext.value, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0
        );
        if (videoStreamIndex < 0 || decoder == nullptr) {
            result.errorMessage = tr("RTMP 输入中没有可解码的视频流。");
            return result;
        }

        const AVCodecParameters *codecParameters =
            formatContext.value->streams[videoStreamIndex]->codecpar;
        if (codecParameters->codec_id != AV_CODEC_ID_H264) {
            result.errorMessage = tr("当前版本只支持 H.264 视频流。");
            return result;
        }

        CodecContextPtr codecContext(avcodec_alloc_context3(decoder));
        if (codecContext == nullptr) {
            result.errorMessage = tr("无法分配 H.264 解码器上下文。");
            return result;
        }

        status = avcodec_parameters_to_context(codecContext.get(), codecParameters);
        if (status < 0) {
            result.errorMessage =
                tr("复制视频流参数失败：%1").arg(ffmpegError(status));
            return result;
        }
        codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;

        status = avcodec_open2(codecContext.get(), decoder, nullptr);
        if (status < 0) {
            result.errorMessage =
                tr("打开 H.264 解码器失败：%1").arg(ffmpegError(status));
            return result;
        }

        PacketPtr packet(av_packet_alloc());
        FramePtr frame(av_frame_alloc());
        if (packet == nullptr || frame == nullptr) {
            result.errorMessage = tr("无法分配 FFmpeg 数据包或视频帧。");
            return result;
        }

        SwsContextHandle swsContext;
        bool playingStatePosted = false;

        const auto receiveFrames = [&]() -> QString {
            while (!stopRequested_.load(std::memory_order_acquire)) {
                const int receiveStatus = avcodec_receive_frame(codecContext.get(), frame.get());
                if (receiveStatus == AVERROR(EAGAIN) || receiveStatus == AVERROR_EOF) {
                    return {};
                }
                if (receiveStatus < 0) {
                    return tr("解码视频帧失败：%1").arg(ffmpegError(receiveStatus));
                }

                if (frame->width <= 0 || frame->height <= 0 || frame->format < 0) {
                    av_frame_unref(frame.get());
                    return tr("解码器返回了无效的视频帧尺寸或像素格式。");
                }

                SwsContext *updatedContext = sws_getCachedContext(
                    swsContext.value,
                    frame->width,
                    frame->height,
                    static_cast<AVPixelFormat>(frame->format),
                    frame->width,
                    frame->height,
                    AV_PIX_FMT_RGB24,
                    SWS_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr
                );
                if (updatedContext == nullptr) {
                    swsContext.value = nullptr;
                    av_frame_unref(frame.get());
                    return tr("创建 RGB24 像素转换上下文失败。");
                }
                swsContext.value = updatedContext;

                QImage image(frame->width, frame->height, QImage::Format_RGB888);
                if (image.isNull()) {
                    av_frame_unref(frame.get());
                    return tr("无法分配 RGB 视频图像。");
                }

                std::array<std::uint8_t *, 4> destinationData {
                    image.bits(), nullptr, nullptr, nullptr
                };
                std::array<int, 4> destinationLinesize {
                    static_cast<int>(image.bytesPerLine()), 0, 0, 0
                };
                const int convertedRows = sws_scale(
                    swsContext.value,
                    frame->data,
                    frame->linesize,
                    0,
                    frame->height,
                    destinationData.data(),
                    destinationLinesize.data()
                );
                av_frame_unref(frame.get());
                if (convertedRows <= 0) {
                    return tr("YUV 到 RGB888 的像素转换失败。");
                }

                result.decodedFrame = true;
                if (!playingStatePosted) {
                    playingStatePosted = true;
                    postState(PlaybackState::Playing, sessionId);
                }
                enqueueFrame(std::move(image), sessionId);
            }
            return {};
        };

        while (!stopRequested_.load(std::memory_order_acquire)) {
            status = av_read_frame(formatContext.value, packet.get());
            if (status < 0) {
                if (!stopRequested_.load(std::memory_order_acquire)) {
                    result.errorMessage =
                        tr("视频流已中断：%1").arg(ffmpegError(status));
                }
                break;
            }

            if (packet->stream_index != videoStreamIndex) {
                av_packet_unref(packet.get());
                continue;
            }

            status = avcodec_send_packet(codecContext.get(), packet.get());
            if (status == AVERROR(EAGAIN)) {
                const QString receiveError = receiveFrames();
                if (!receiveError.isEmpty()) {
                    result.errorMessage = receiveError;
                    av_packet_unref(packet.get());
                    break;
                }
                status = avcodec_send_packet(codecContext.get(), packet.get());
            }
            av_packet_unref(packet.get());

            if (status < 0) {
                result.errorMessage =
                    tr("提交 H.264 数据包失败：%1").arg(ffmpegError(status));
                break;
            }

            const QString receiveError = receiveFrames();
            if (!receiveError.isEmpty()) {
                result.errorMessage = receiveError;
                break;
            }
        }

        return result;
    };

    while (!stopRequested_.load(std::memory_order_acquire)) {
        postState(
            firstAttempt ? PlaybackState::Connecting : PlaybackState::Reconnecting,
            sessionId
        );

        DecodeAttemptResult result = decodeAttempt();
        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }

        if (result.decodedFrame) {
            reconnectDelayIndex = 0;
        }
        const int reconnectDelayMs = kReconnectDelaysMs.at(reconnectDelayIndex);
        if (!result.decodedFrame) {
            reconnectDelayIndex = std::min(
                reconnectDelayIndex + 1,
                static_cast<int>(kReconnectDelaysMs.size()) - 1
            );
        }

        postState(PlaybackState::Reconnecting, sessionId);
        if (!result.errorMessage.isEmpty()) {
            postError(
                tr("%1；%2 秒后重试。")
                    .arg(result.errorMessage)
                    .arg(reconnectDelayMs / 1000),
                sessionId
            );
        }

        firstAttempt = false;
        if (!waitForReconnect(reconnectDelayMs)) {
            break;
        }
    }

    avformat_network_deinit();
    postState(PlaybackState::Stopped, sessionId);
}

void FFmpegPlayer::enqueueFrame(QImage image, std::uint64_t sessionId)
{
    bool scheduleDelivery = false;
    {
        const std::lock_guard<std::mutex> lock(frameMutex_);
        if (sessionId != sessionId_.load(std::memory_order_acquire)) {
            return;
        }
        pendingFrame_ = std::move(image);
        if (!frameDeliveryScheduled_) {
            frameDeliveryScheduled_ = true;
            scheduleDelivery = true;
        }
    }

    if (scheduleDelivery) {
        QMetaObject::invokeMethod(
            this,
            [this, sessionId] { deliverLatestFrame(sessionId); },
            Qt::QueuedConnection
        );
    }
}

void FFmpegPlayer::deliverLatestFrame(std::uint64_t sessionId)
{
    Q_ASSERT(QThread::currentThread() == thread());

    QImage frame;
    {
        const std::lock_guard<std::mutex> lock(frameMutex_);
        if (sessionId != sessionId_.load(std::memory_order_acquire)) {
            pendingFrame_ = QImage();
            frameDeliveryScheduled_ = false;
            return;
        }
        frame = std::move(pendingFrame_);
        pendingFrame_ = QImage();
        frameDeliveryScheduled_ = false;
    }

    if (!frame.isNull()) {
        emit frameReady(frame);
    }
}

void FFmpegPlayer::postState(PlaybackState state, std::uint64_t sessionId)
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
    const bool stopped = reconnectCondition_.wait_for(
        lock,
        std::chrono::milliseconds(delayMs),
        [this] { return stopRequested_.load(std::memory_order_acquire); }
    );
    return !stopped;
}

int FFmpegPlayer::interruptCallback(void *opaque) noexcept
{
    const auto *player = static_cast<const FFmpegPlayer *>(opaque);
    return player->stopRequested_.load(std::memory_order_acquire) ? 1 : 0;
}
