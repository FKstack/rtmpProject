#include "publisher/Mp4H264PublisherSource.h"

extern "C" {
#include <libavcodec/bsf.h>
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

namespace rtmp_monitor::publisher {
namespace {

constexpr std::size_t kMaximumAccessUnitBytes = 4U * 1024U * 1024U;

struct FormatContextOwner
{
    AVFormatContext *value = nullptr;
    ~FormatContextOwner()
    {
        if (value) avformat_close_input(&value);
    }
};

struct BsfContextOwner
{
    AVBSFContext *value = nullptr;
    ~BsfContextOwner()
    {
        av_bsf_free(&value);
    }
};

struct PacketOwner
{
    AVPacket *value = av_packet_alloc();
    ~PacketOwner()
    {
        av_packet_free(&value);
    }
};

bool submitAccepted(H264SubmitResult result)
{
    return result == H264SubmitResult::Accepted ||
           result == H264SubmitResult::AcceptedAfterDrop;
}

bool submitDropped(H264SubmitResult result)
{
    return result == H264SubmitResult::DroppedCapacity ||
           result == H264SubmitResult::DroppedUntilKeyframe;
}

} // namespace

class Mp4H264PublisherSource::Impl final
{
public:
    ~Impl()
    {
        stop();
    }

    PublisherSourceError start(
        std::string filePath,
        PublisherSubmitCallback submit
    )
    {
        if (filePath.empty() || !submit || started_) {
            return PublisherSourceError::InvalidState;
        }
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(filePath))) {
            return PublisherSourceError::FileNotFound;
        }
        started_ = true;
        {
            const std::lock_guard lock(mutex_);
            snapshot_.running = true;
        }
        worker_ = std::thread(
            [this, filePath = std::move(filePath), submit = std::move(submit)]() mutable {
                run(filePath, submit);
            }
        );
        return PublisherSourceError::None;
    }

    PublisherSourceError waitForCompletion(std::chrono::milliseconds timeout)
    {
        {
            std::unique_lock lock(mutex_);
            if (!started_) return PublisherSourceError::InvalidState;
            if (!changed_.wait_for(lock, timeout, [this] {
                    return snapshot_.completed || !snapshot_.running;
                })) {
                return PublisherSourceError::Timeout;
            }
        }
        joinWorker();
        const std::lock_guard lock(mutex_);
        return snapshot_.error;
    }

    PublisherSourceSnapshot snapshot() const noexcept
    {
        const std::lock_guard lock(mutex_);
        return snapshot_;
    }

    void stop() noexcept
    {
        {
            const std::lock_guard lock(mutex_);
            stopRequested_ = true;
            changed_.notify_all();
        }
        joinWorker();
    }

private:
    void joinWorker() noexcept
    {
        const std::lock_guard lock(joinMutex_);
        if (worker_.joinable()) worker_.join();
    }

    bool stopRequested() const
    {
        const std::lock_guard lock(mutex_);
        return stopRequested_;
    }

    bool waitUntil(std::chrono::steady_clock::time_point deadline)
    {
        std::unique_lock lock(mutex_);
        return !changed_.wait_until(lock, deadline, [this] {
            return stopRequested_;
        });
    }

    void finish(PublisherSourceError error)
    {
        const std::lock_guard lock(mutex_);
        snapshot_.running = false;
        snapshot_.completed = error == PublisherSourceError::None;
        snapshot_.error = error;
        changed_.notify_all();
    }

    void run(
        const std::string &filePath,
        const PublisherSubmitCallback &submit
    )
    {
        FormatContextOwner format;
        if (avformat_open_input(&format.value, filePath.c_str(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(format.value, nullptr) < 0) {
            finish(PublisherSourceError::OpenFailed);
            return;
        }

        const int videoIndex = av_find_best_stream(
            format.value, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0
        );
        if (videoIndex < 0) {
            finish(PublisherSourceError::VideoStreamMissing);
            return;
        }
        AVStream *const stream = format.value->streams[videoIndex];
        if (!stream || stream->codecpar->codec_id != AV_CODEC_ID_H264) {
            finish(PublisherSourceError::H264Required);
            return;
        }
        if (stream->codecpar->video_delay > 0) {
            finish(PublisherSourceError::BFramesUnsupported);
            return;
        }

        const AVBitStreamFilter *const filter =
            av_bsf_get_by_name("h264_mp4toannexb");
        BsfContextOwner bsf;
        if (!filter || av_bsf_alloc(filter, &bsf.value) < 0 ||
            avcodec_parameters_copy(bsf.value->par_in, stream->codecpar) < 0) {
            finish(PublisherSourceError::BitstreamFilterFailure);
            return;
        }
        bsf.value->time_base_in = stream->time_base;
        if (av_bsf_init(bsf.value) < 0) {
            finish(PublisherSourceError::BitstreamFilterFailure);
            return;
        }

        PacketOwner input;
        PacketOwner output;
        if (!input.value || !output.value) {
            finish(PublisherSourceError::ReadFailure);
            return;
        }

        bool haveOrigin = false;
        std::int64_t firstPtsUs = 0;
        std::int64_t firstDtsUs = 0;
        std::chrono::steady_clock::time_point startedAt;
        PublisherSourceError error = PublisherSourceError::None;

        const auto emitPacket = [&](AVPacket *packet) -> bool {
            if (!packet || packet->size <= 0 ||
                static_cast<std::size_t>(packet->size) > kMaximumAccessUnitBytes) {
                error = PublisherSourceError::ReadFailure;
                return false;
            }
            const std::int64_t rawPts = packet->pts != AV_NOPTS_VALUE
                                            ? packet->pts
                                            : packet->dts;
            const std::int64_t rawDts = packet->dts != AV_NOPTS_VALUE
                                            ? packet->dts
                                            : rawPts;
            if (rawPts == AV_NOPTS_VALUE || rawDts == AV_NOPTS_VALUE) {
                error = PublisherSourceError::ReadFailure;
                return false;
            }
            const std::int64_t ptsUs = av_rescale_q(
                rawPts, bsf.value->time_base_out, AVRational {1, 1000000}
            );
            const std::int64_t dtsUs = av_rescale_q(
                rawDts, bsf.value->time_base_out, AVRational {1, 1000000}
            );
            if (!haveOrigin) {
                haveOrigin = true;
                firstPtsUs = ptsUs;
                firstDtsUs = dtsUs;
                startedAt = std::chrono::steady_clock::now();
            }

            const auto pacingOffset = std::chrono::microseconds(
                std::max<std::int64_t>(0, dtsUs - firstDtsUs)
            );
            if (!waitUntil(startedAt + pacingOffset)) {
                error = PublisherSourceError::Stopped;
                return false;
            }

            H264AccessUnit accessUnit;
            accessUnit.annexB.assign(packet->data, packet->data + packet->size);
            accessUnit.mediaTimestampUs =
                std::max<std::int64_t>(0, ptsUs - firstPtsUs);
            accessUnit.keyFrame = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            if (!isValidH264AccessUnit(accessUnit, kMaximumAccessUnitBytes)) {
                error = PublisherSourceError::ReadFailure;
                return false;
            }

            const H264SubmitResult submitResult = submit(std::move(accessUnit));
            const std::lock_guard lock(mutex_);
            if (submitAccepted(submitResult)) {
                ++snapshot_.emittedAccessUnits;
                if ((packet->flags & AV_PKT_FLAG_KEY) != 0) {
                    ++snapshot_.emittedKeyframes;
                }
                return true;
            }
            if (submitDropped(submitResult)) {
                ++snapshot_.droppedAccessUnits;
                return true;
            }
            error = stopRequested_
                        ? PublisherSourceError::Stopped
                        : PublisherSourceError::SubmitFailure;
            return false;
        };

        while (!stopRequested()) {
            const int readStatus = av_read_frame(format.value, input.value);
            if (readStatus == AVERROR_EOF) break;
            if (readStatus < 0) {
                error = PublisherSourceError::ReadFailure;
                break;
            }
            if (input.value->stream_index != videoIndex) {
                av_packet_unref(input.value);
                continue;
            }
            if (av_bsf_send_packet(bsf.value, input.value) < 0) {
                av_packet_unref(input.value);
                error = PublisherSourceError::BitstreamFilterFailure;
                break;
            }
            av_packet_unref(input.value);

            for (;;) {
                const int receiveStatus = av_bsf_receive_packet(bsf.value, output.value);
                if (receiveStatus == AVERROR(EAGAIN) || receiveStatus == AVERROR_EOF) {
                    break;
                }
                if (receiveStatus < 0 || !emitPacket(output.value)) {
                    if (receiveStatus < 0) {
                        error = PublisherSourceError::BitstreamFilterFailure;
                    }
                    av_packet_unref(output.value);
                    break;
                }
                av_packet_unref(output.value);
            }
            if (error != PublisherSourceError::None) break;
        }

        if (error == PublisherSourceError::None && !stopRequested()) {
            if (av_bsf_send_packet(bsf.value, nullptr) < 0) {
                error = PublisherSourceError::BitstreamFilterFailure;
            } else {
                for (;;) {
                    const int receiveStatus = av_bsf_receive_packet(bsf.value, output.value);
                    if (receiveStatus == AVERROR_EOF || receiveStatus == AVERROR(EAGAIN)) {
                        break;
                    }
                    if (receiveStatus < 0 || !emitPacket(output.value)) {
                        if (receiveStatus < 0) {
                            error = PublisherSourceError::BitstreamFilterFailure;
                        }
                        av_packet_unref(output.value);
                        break;
                    }
                    av_packet_unref(output.value);
                }
            }
        }
        if (error == PublisherSourceError::None && stopRequested()) {
            error = PublisherSourceError::Stopped;
        }
        finish(error);
    }

    mutable std::mutex mutex_;
    std::mutex joinMutex_;
    std::condition_variable changed_;
    PublisherSourceSnapshot snapshot_;
    bool started_ = false;
    bool stopRequested_ = false;
    std::thread worker_;
};

Mp4H264PublisherSource::Mp4H264PublisherSource()
    : impl_(std::make_unique<Impl>())
{
}

Mp4H264PublisherSource::~Mp4H264PublisherSource() = default;

PublisherSourceError Mp4H264PublisherSource::start(
    std::string filePath,
    PublisherSubmitCallback submit
)
{
    return impl_->start(std::move(filePath), std::move(submit));
}

PublisherSourceError Mp4H264PublisherSource::waitForCompletion(
    std::chrono::milliseconds timeout
)
{
    return impl_->waitForCompletion(timeout);
}

PublisherSourceSnapshot Mp4H264PublisherSource::snapshot() const noexcept
{
    return impl_->snapshot();
}

void Mp4H264PublisherSource::stop() noexcept
{
    impl_->stop();
}

const char *Mp4H264PublisherSource::errorName(
    PublisherSourceError error
) noexcept
{
    switch (error) {
    case PublisherSourceError::None: return "none";
    case PublisherSourceError::InvalidState: return "invalid_state";
    case PublisherSourceError::FileNotFound: return "file_not_found";
    case PublisherSourceError::OpenFailed: return "open_failed";
    case PublisherSourceError::VideoStreamMissing: return "video_stream_missing";
    case PublisherSourceError::H264Required: return "h264_required";
    case PublisherSourceError::BFramesUnsupported: return "b_frames_unsupported";
    case PublisherSourceError::BitstreamFilterFailure: return "bitstream_filter_failure";
    case PublisherSourceError::ReadFailure: return "read_failure";
    case PublisherSourceError::SubmitFailure: return "submit_failure";
    case PublisherSourceError::Stopped: return "stopped";
    case PublisherSourceError::Timeout: return "timeout";
    case PublisherSourceError::PlatformUnsupported: return "platform_unsupported";
    case PublisherSourceError::CameraNotFound: return "camera_not_found";
    case PublisherSourceError::CompatiblePathUnavailable:
        return "compatible_path_unavailable";
    case PublisherSourceError::DeviceLost: return "device_lost";
    case PublisherSourceError::EncoderValidationFailed:
        return "encoder_validation_failed";
    }
    return "unknown";
}

} // namespace rtmp_monitor::publisher
