#include "media/VideoFrameConverter.h"

#include <array>
#include <limits>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace {

AVPixelFormat sourceFormat(VideoPixelFormat format)
{
    return format == VideoPixelFormat::Yuv420P8 ? AV_PIX_FMT_YUV420P
                                                 : AV_PIX_FMT_NV12;
}

int swsColorSpace(VideoMatrixCoefficients matrix)
{
    switch (matrix) {
    case VideoMatrixCoefficients::Bt709:
        return SWS_CS_ITU709;
    case VideoMatrixCoefficients::Bt2020Ncl:
        return SWS_CS_BT2020;
    case VideoMatrixCoefficients::Bt601:
    case VideoMatrixCoefficients::Unknown:
        return SWS_CS_ITU601;
    }
    return SWS_CS_ITU601;
}

} // namespace

VideoFrameToImageConverter::~VideoFrameToImageConverter()
{
    reset();
}

QImage VideoFrameToImageConverter::convert(const VideoFrame &frame)
{
    if (!frame.isValid() || !isSupportedSdrTransfer(frame.color().transfer)) {
        return {};
    }

    std::array<const std::uint8_t *, 4> sourceData {};
    std::array<int, 4> sourceStride {};
    for (int index = 0; index < frame.planeCount(); ++index) {
        const VideoPlaneView plane = frame.plane(index);
        if (plane.stride < std::numeric_limits<int>::min() ||
            plane.stride > std::numeric_limits<int>::max()) {
            return {};
        }
        sourceData[index] = plane.data;
        sourceStride[index] = static_cast<int>(plane.stride);
    }

    auto *context = static_cast<SwsContext *>(context_);
    context = sws_getCachedContext(
        context,
        frame.width(),
        frame.height(),
        sourceFormat(frame.pixelFormat()),
        frame.width(),
        frame.height(),
        AV_PIX_FMT_RGB24,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );
    context_ = context;
    if (context == nullptr) {
        return {};
    }

    const VideoColorDescription color = resolvedVideoColorDescription(
        frame.color(), frame.width(), frame.height()
    );
    const int *coefficients = sws_getCoefficients(swsColorSpace(color.matrix));
    sws_setColorspaceDetails(
        context,
        coefficients,
        color.range == VideoColorRange::Full ? 1 : 0,
        coefficients,
        1,
        0,
        1 << 16,
        1 << 16
    );

    QImage image(frame.width(), frame.height(), QImage::Format_RGB888);
    if (image.isNull()) {
        return {};
    }
    std::array<std::uint8_t *, 4> destinationData {image.bits(), nullptr, nullptr, nullptr};
    std::array<int, 4> destinationStride {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    const int rows = sws_scale(
        context,
        sourceData.data(),
        sourceStride.data(),
        0,
        frame.height(),
        destinationData.data(),
        destinationStride.data()
    );
    return rows == frame.height() ? image : QImage {};
}

void VideoFrameToImageConverter::reset() noexcept
{
    if (context_ != nullptr) {
        sws_freeContext(static_cast<SwsContext *>(context_));
        context_ = nullptr;
    }
}
