#include "FfmpegInputSession.h"
#include "FfmpegSessionTypes.h"

#include <QByteArray>
#include <QObject>

#include <array>
#include <cerrno>
#include <chrono>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

namespace {

constexpr int kNetworkTimeoutMicroseconds = 3'000'000;

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           ).count();
}

QString ffmpegError(int errorCode)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer {};
    if (av_strerror(errorCode, buffer.data(), buffer.size()) < 0) {
        return QObject::tr("未知 FFmpeg 错误 (%1)").arg(errorCode);
    }
    return QString::fromUtf8(buffer.data());
}

PlaybackErrorCode networkErrorCode(int nativeCode)
{
    if (nativeCode == AVERROR(ETIMEDOUT)) {
        return PlaybackErrorCode::ConnectionTimeout;
    }
    return PlaybackErrorCode::HostUnavailable;
}

class FFmpegNetworkRuntime final
{
public:
    FFmpegNetworkRuntime() : initialized_(avformat_network_init() >= 0) {}
    ~FFmpegNetworkRuntime()
    {
        if (initialized_) avformat_network_deinit();
    }
    [[nodiscard]] bool available() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
};

FFmpegNetworkRuntime &networkRuntime()
{
    static FFmpegNetworkRuntime runtime;
    return runtime;
}

struct FormatContextHandle
{
    AVFormatContext *value = nullptr;
    ~FormatContextHandle()
    {
        if (value != nullptr) avformat_close_input(&value);
    }
};

struct DictionaryHandle
{
    AVDictionary *value = nullptr;
    ~DictionaryHandle() { av_dict_free(&value); }
};

} // namespace

FfmpegInputSession::FfmpegInputSession(
    const std::atomic_bool &stopRequested,
    const std::atomic_bool &restartRequested,
    ConfigurationCallback configurationCallback,
    PacketCallback packetCallback
)
    : stopRequested_(stopRequested)
    , restartRequested_(restartRequested)
    , configurationCallback_(std::move(configurationCallback))
    , packetCallback_(std::move(packetCallback))
{
}

bool FfmpegInputSession::networkRuntimeAvailable() noexcept
{
    return networkRuntime().available();
}

FfmpegInputResult FfmpegInputSession::run(
    const QString &rtmpUrl,
    std::uint64_t sessionId
)
{
    FfmpegInputResult result;
    FormatContextHandle formatContext;
    formatContext.value = avformat_alloc_context();
    if (formatContext.value == nullptr) {
        result.message = QObject::tr("无法分配 FFmpeg 输入上下文。");
        result.errorCode = PlaybackErrorCode::ResourceFailure;
        return result;
    }
    formatContext.value->interrupt_callback = {
        &FfmpegInputSession::interruptCallback, this
    };

    DictionaryHandle inputOptions;
    const QByteArray timeout = QByteArray::number(kNetworkTimeoutMicroseconds);
    av_dict_set(&inputOptions.value, "rtmp_live", "live", 0);
    av_dict_set(&inputOptions.value, "rtmp_buffer", "0", 0);
    av_dict_set(&inputOptions.value, "tcp_nodelay", "1", 0);
    av_dict_set(&inputOptions.value, "rw_timeout", timeout.constData(), 0);
    av_dict_set(&inputOptions.value, "fflags", "nobuffer", 0);
    av_dict_set(&inputOptions.value, "max_delay", "0", 0);
    av_dict_set(&inputOptions.value, "probesize", "4096", 0);
    av_dict_set(&inputOptions.value, "fpsprobesize", "0", 0);
    // RTMP/FLV carries codec sequence headers at connection time. Keeping a
    // one-second analysis window makes avformat_find_stream_info retain old
    // live packets and creates a fixed A/V playback delay. 50 ms is enough
    // for the H.264/AAC headers while preserving a bounded fallback window.
    av_dict_set(&inputOptions.value, "analyzeduration", "50000", 0);

    const QByteArray encodedUrl = rtmpUrl.toUtf8();
    int status = avformat_open_input(
        &formatContext.value, encodedUrl.constData(), nullptr, &inputOptions.value
    );
    if (status < 0) {
        if (!interrupted()) {
            result.message = QObject::tr("打开 RTMP 输入失败：%1").arg(ffmpegError(status));
            result.nativeCode = status;
            result.errorCode = networkErrorCode(status);
        }
        return result;
    }

    const AVCodec *decoder = nullptr;
    int videoStreamIndex = av_find_best_stream(
        formatContext.value, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0
    );
    const bool sequenceHeaderReady = videoStreamIndex >= 0 &&
        formatContext.value->streams[videoStreamIndex]->codecpar != nullptr &&
        formatContext.value->streams[videoStreamIndex]->codecpar->codec_id ==
            AV_CODEC_ID_H264 &&
        formatContext.value->streams[videoStreamIndex]->codecpar->extradata_size > 0;
    if (!sequenceHeaderReady) {
        // A normal live FLV supplies H.264/AAC sequence headers during open.
        // Probe only unusual publishers; probing every connection retains live
        // packets and turns them into a fixed playback backlog.
        status = avformat_find_stream_info(formatContext.value, nullptr);
        if (status < 0) {
            result.message =
                QObject::tr("读取流信息失败：%1").arg(ffmpegError(status));
            result.nativeCode = status;
            result.errorCode = PlaybackErrorCode::MediaUnavailable;
            return result;
        }

        decoder = nullptr;
        videoStreamIndex = av_find_best_stream(
            formatContext.value, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0
        );
    }
    if (videoStreamIndex < 0 || decoder == nullptr) {
        result.message = QObject::tr("RTMP 输入中没有可解码的视频流。");
        result.nativeCode = videoStreamIndex;
        result.errorCode = PlaybackErrorCode::MediaUnavailable;
        return result;
    }

    const AVCodecParameters *parameters =
        formatContext.value->streams[videoStreamIndex]->codecpar;
    if (parameters->codec_id != AV_CODEC_ID_H264) {
        result.message = QObject::tr("当前版本只支持 H.264 视频流。");
        result.nativeCode = static_cast<int>(parameters->codec_id);
        result.errorCode = PlaybackErrorCode::UnsupportedMedia;
        return result;
    }

    auto configuration = std::make_shared<FfmpegCodecConfiguration>();
    if (configuration->parameters == nullptr ||
        avcodec_parameters_copy(configuration->parameters, parameters) < 0) {
        result.message = QObject::tr("复制视频流参数失败。");
        result.errorCode = PlaybackErrorCode::ResourceFailure;
        return result;
    }
    configuration->timeBase = formatContext.value->streams[videoStreamIndex]->time_base;
    configuration->kind = FfmpegTrackKind::Video;
    configuration->streamIndex = videoStreamIndex;
    configurationCallback_(std::static_pointer_cast<void>(configuration), sessionId);

    const int audioStreamIndex = av_find_best_stream(
        formatContext.value, AVMEDIA_TYPE_AUDIO, -1, videoStreamIndex, nullptr, 0
    );
    if (audioStreamIndex >= 0) {
        const AVCodecParameters *audioParameters =
            formatContext.value->streams[audioStreamIndex]->codecpar;
        auto audioConfiguration = std::make_shared<FfmpegCodecConfiguration>();
        if (audioConfiguration->parameters != nullptr &&
            avcodec_parameters_copy(
                audioConfiguration->parameters, audioParameters
            ) >= 0) {
            audioConfiguration->timeBase =
                formatContext.value->streams[audioStreamIndex]->time_base;
            audioConfiguration->kind = FfmpegTrackKind::Audio;
            audioConfiguration->streamIndex = audioStreamIndex;
            configurationCallback_(
                std::static_pointer_cast<void>(audioConfiguration), sessionId
            );
        }
    }

    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        result.message = QObject::tr("无法分配视频数据包。");
        result.errorCode = PlaybackErrorCode::ResourceFailure;
        return result;
    }
    while (!interrupted()) {
        status = av_read_frame(formatContext.value, packet);
        if (status < 0) {
            if (!interrupted()) {
                result.message = QObject::tr("视频流已中断：%1").arg(ffmpegError(status));
                result.nativeCode = status;
                result.errorCode = networkErrorCode(status);
            }
            break;
        }
        const bool videoPacket = packet->stream_index == videoStreamIndex;
        const bool audioPacket =
            audioStreamIndex >= 0 && packet->stream_index == audioStreamIndex;
        if (!videoPacket && !audioPacket) {
            av_packet_unref(packet);
            continue;
        }

        AVPacket *ownedPacket = av_packet_alloc();
        if (ownedPacket == nullptr) {
            av_packet_unref(packet);
            result.message = QObject::tr("无法复制视频数据包。");
            result.errorCode = PlaybackErrorCode::ResourceFailure;
            break;
        }
        av_packet_move_ref(ownedPacket, packet);
        result.receivedPackets = true;
        packetCallback_(
            videoPacket ? FfmpegTrackKind::Video : FfmpegTrackKind::Audio,
            ownedPacket,
            monotonicMilliseconds(),
            sessionId
        );
    }
    av_packet_free(&packet);
    return result;
}

int FfmpegInputSession::interruptCallback(void *opaque) noexcept
{
    return static_cast<const FfmpegInputSession *>(opaque)->interrupted() ? 1 : 0;
}

bool FfmpegInputSession::interrupted() const noexcept
{
    return stopRequested_.load(std::memory_order_acquire) ||
           restartRequested_.load(std::memory_order_acquire);
}
