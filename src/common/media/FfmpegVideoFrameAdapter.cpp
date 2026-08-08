#include "FfmpegVideoFrameAdapter.h"

#include <array>
#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {

VideoColorPrimaries mapPrimaries(AVColorPrimaries value)
{
    switch (value) {
    case AVCOL_PRI_BT709:
        return VideoColorPrimaries::Bt709;
    case AVCOL_PRI_BT470BG:
        return VideoColorPrimaries::Bt601_625;
    case AVCOL_PRI_SMPTE170M:
        return VideoColorPrimaries::Bt601_525;
    case AVCOL_PRI_BT2020:
        return VideoColorPrimaries::Bt2020;
    default:
        return VideoColorPrimaries::Unknown;
    }
}

VideoTransferFunction mapTransfer(AVColorTransferCharacteristic value)
{
    switch (value) {
    case AVCOL_TRC_BT709:
    case AVCOL_TRC_SMPTE170M:
        return VideoTransferFunction::Bt709;
    case AVCOL_TRC_IEC61966_2_1:
        return VideoTransferFunction::Srgb;
    case AVCOL_TRC_BT2020_10:
        return VideoTransferFunction::Bt2020_10;
    case AVCOL_TRC_SMPTE2084:
        return VideoTransferFunction::Pq;
    case AVCOL_TRC_ARIB_STD_B67:
        return VideoTransferFunction::Hlg;
    default:
        return VideoTransferFunction::Unknown;
    }
}

VideoMatrixCoefficients mapMatrix(AVColorSpace value)
{
    switch (value) {
    case AVCOL_SPC_BT709:
        return VideoMatrixCoefficients::Bt709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return VideoMatrixCoefficients::Bt601;
    case AVCOL_SPC_BT2020_NCL:
        return VideoMatrixCoefficients::Bt2020Ncl;
    default:
        return VideoMatrixCoefficients::Unknown;
    }
}

VideoColorRange mapRange(AVColorRange value)
{
    switch (value) {
    case AVCOL_RANGE_MPEG:
        return VideoColorRange::Limited;
    case AVCOL_RANGE_JPEG:
        return VideoColorRange::Full;
    default:
        return VideoColorRange::Unknown;
    }
}

} // namespace

std::optional<VideoFrame> FfmpegVideoFrameAdapter::adapt(
    const AVFrame *frame,
    VideoRational timeBase,
    std::uint64_t sequence,
    std::uint64_t sessionGeneration,
    qint64 receivedMonotonicMs,
    qint64 sourceTimestampMs
)
{
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) {
        return std::nullopt;
    }

    VideoPixelFormat format;
    int planeCount = 0;
    if (frame->format == AV_PIX_FMT_YUV420P ||
        frame->format == AV_PIX_FMT_YUVJ420P) {
        format = VideoPixelFormat::Yuv420P8;
        planeCount = 3;
    } else if (frame->format == AV_PIX_FMT_NV12) {
        format = VideoPixelFormat::Nv12_8;
        planeCount = 2;
    } else {
        return std::nullopt;
    }

    AVFrame *clone = av_frame_clone(frame);
    if (clone == nullptr) {
        return std::nullopt;
    }
    const auto deleter = [](const void *opaque) {
        auto *owned = const_cast<AVFrame *>(static_cast<const AVFrame *>(opaque));
        av_frame_free(&owned);
    };
    std::shared_ptr<const void> owner(clone, deleter);

    const int chromaWidth = (clone->width + 1) / 2;
    const int chromaHeight = (clone->height + 1) / 2;
    std::array<VideoPlaneView, VideoFrame::kMaximumPlanes> planes {};
    planes[0] = {clone->data[0], clone->linesize[0], clone->width, clone->height};
    if (format == VideoPixelFormat::Yuv420P8) {
        planes[1] = {clone->data[1], clone->linesize[1], chromaWidth, chromaHeight};
        planes[2] = {clone->data[2], clone->linesize[2], chromaWidth, chromaHeight};
    } else {
        planes[1] = {
            clone->data[1], clone->linesize[1], chromaWidth * 2, chromaHeight
        };
    }

    VideoColorDescription color {
        mapPrimaries(clone->color_primaries),
        mapTransfer(clone->color_trc),
        mapMatrix(clone->colorspace),
        mapRange(clone->color_range)
    };
    if (frame->format == AV_PIX_FMT_YUVJ420P) {
        color.range = VideoColorRange::Full;
    }

    VideoFrame result(
        clone->width,
        clone->height,
        format,
        planeCount,
        planes,
        std::move(owner),
        clone->pts,
        clone->duration,
        timeBase,
        resolvedVideoColorDescription(color, clone->width, clone->height),
        sequence,
        sessionGeneration,
        receivedMonotonicMs,
        sourceTimestampMs
    );
    return result.isValid() ? std::optional<VideoFrame>(std::move(result))
                            : std::nullopt;
}
