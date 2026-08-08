#include "media/VideoFrame.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

struct OwnedPlanes
{
    std::array<std::vector<std::uint8_t>, VideoFrame::kMaximumPlanes> bytes;
};

int expectedPlaneCount(VideoPixelFormat format)
{
    return format == VideoPixelFormat::Yuv420P8 ? 3 : 2;
}

std::array<std::pair<int, int>, VideoFrame::kMaximumPlanes> planeGeometry(
    int width,
    int height,
    VideoPixelFormat format
)
{
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    if (format == VideoPixelFormat::Yuv420P8) {
        return {{{width, height}, {chromaWidth, chromaHeight},
                 {chromaWidth, chromaHeight}}};
    }
    return {{{width, height}, {chromaWidth * 2, chromaHeight}, {0, 0}}};
}

} // namespace

VideoFrame::VideoFrame(
    int width,
    int height,
    VideoPixelFormat format,
    int planeCount,
    std::array<VideoPlaneView, kMaximumPlanes> planes,
    std::shared_ptr<const void> owner,
    qint64 pts,
    qint64 duration,
    VideoRational timeBase,
    VideoColorDescription color,
    std::uint64_t sequence,
    std::uint64_t sessionGeneration,
    qint64 receivedMonotonicMs,
    qint64 sourceTimestampMs
)
    : width_(width)
    , height_(height)
    , pixelFormat_(format)
    , planeCount_(planeCount)
    , planes_(planes)
    , owner_(std::move(owner))
    , pts_(pts)
    , duration_(duration)
    , timeBase_(timeBase)
    , color_(color)
    , sequence_(sequence)
    , sessionGeneration_(sessionGeneration)
    , receivedMonotonicMs_(receivedMonotonicMs)
    , sourceTimestampMs_(sourceTimestampMs)
{
}

bool VideoFrame::isValid() const noexcept
{
    if (width_ <= 0 || height_ <= 0 || owner_ == nullptr || sequence_ == 0 ||
        planeCount_ != expectedPlaneCount(pixelFormat_)) {
        return false;
    }
    const auto geometry = planeGeometry(width_, height_, pixelFormat_);
    for (int index = 0; index < planeCount_; ++index) {
        const VideoPlaneView &view = planes_[index];
        if (view.data == nullptr || view.rows != geometry[index].second ||
            view.rowBytes < geometry[index].first ||
            std::abs(view.stride) < view.rowBytes) {
            return false;
        }
    }
    return true;
}

VideoPlaneView VideoFrame::plane(int index) const noexcept
{
    return index >= 0 && index < planeCount_ ? planes_[index] : VideoPlaneView {};
}

std::optional<VideoFrame> VideoFrame::copyFromPlanes(
    int width,
    int height,
    VideoPixelFormat format,
    const std::array<VideoPlaneView, kMaximumPlanes> &planes,
    qint64 pts,
    qint64 duration,
    VideoRational timeBase,
    VideoColorDescription color,
    std::uint64_t sequence,
    std::uint64_t sessionGeneration,
    qint64 receivedMonotonicMs,
    qint64 sourceTimestampMs
)
{
    if (width <= 0 || height <= 0 || sequence == 0) {
        return std::nullopt;
    }

    const int count = expectedPlaneCount(format);
    const auto geometry = planeGeometry(width, height, format);
    auto storage = std::make_shared<OwnedPlanes>();
    std::array<VideoPlaneView, kMaximumPlanes> ownedViews {};
    for (int index = 0; index < count; ++index) {
        const VideoPlaneView &source = planes[index];
        const int rowBytes = geometry[index].first;
        const int rows = geometry[index].second;
        if (source.data == nullptr || source.rows < rows ||
            source.rowBytes < rowBytes || std::abs(source.stride) < rowBytes) {
            return std::nullopt;
        }
        auto &destination = storage->bytes[index];
        destination.resize(static_cast<std::size_t>(rowBytes) * rows);
        const std::uint8_t *sourceRow = source.data;
        for (int row = 0; row < rows; ++row) {
            std::memcpy(
                destination.data() + static_cast<std::size_t>(row) * rowBytes,
                sourceRow,
                static_cast<std::size_t>(rowBytes)
            );
            sourceRow += source.stride;
        }
        ownedViews[index] = {
            destination.data(), rowBytes, rowBytes, rows
        };
    }

    return VideoFrame(
        width,
        height,
        format,
        count,
        ownedViews,
        std::static_pointer_cast<const void>(storage),
        pts,
        duration,
        timeBase,
        resolvedVideoColorDescription(color, width, height),
        sequence,
        sessionGeneration,
        receivedMonotonicMs,
        sourceTimestampMs
    );
}

VideoColorDescription resolvedVideoColorDescription(
    const VideoColorDescription &source,
    int width,
    int height
) noexcept
{
    VideoColorDescription result = source;
    const bool highDefinition = width >= 1280 || height >= 720;
    if (result.matrix == VideoMatrixCoefficients::Unknown) {
        result.matrix = highDefinition
                            ? VideoMatrixCoefficients::Bt709
                            : VideoMatrixCoefficients::Bt601;
    }
    if (result.primaries == VideoColorPrimaries::Unknown) {
        result.primaries = highDefinition
                               ? VideoColorPrimaries::Bt709
                               : VideoColorPrimaries::Bt601_625;
    }
    if (result.transfer == VideoTransferFunction::Unknown) {
        result.transfer = VideoTransferFunction::Bt709;
    }
    if (result.range == VideoColorRange::Unknown) {
        result.range = VideoColorRange::Limited;
    }
    return result;
}

bool isSupportedSdrTransfer(VideoTransferFunction transfer) noexcept
{
    return transfer == VideoTransferFunction::Unknown ||
           transfer == VideoTransferFunction::Bt709 ||
           transfer == VideoTransferFunction::Srgb ||
           transfer == VideoTransferFunction::Bt2020_10;
}
