#pragma once

#include <QtGlobal>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

enum class VideoPixelFormat {
    Yuv420P8,
    Nv12_8,
};

enum class VideoColorPrimaries {
    Unknown,
    Bt601_525,
    Bt601_625,
    Bt709,
    Bt2020,
};

enum class VideoTransferFunction {
    Unknown,
    Bt709,
    Srgb,
    Bt2020_10,
    Pq,
    Hlg,
};

enum class VideoMatrixCoefficients {
    Unknown,
    Bt601,
    Bt709,
    Bt2020Ncl,
};

enum class VideoColorRange {
    Unknown,
    Limited,
    Full,
};

struct VideoRational
{
    int numerator = 0;
    int denominator = 1;
};

struct VideoColorDescription
{
    VideoColorPrimaries primaries = VideoColorPrimaries::Unknown;
    VideoTransferFunction transfer = VideoTransferFunction::Unknown;
    VideoMatrixCoefficients matrix = VideoMatrixCoefficients::Unknown;
    VideoColorRange range = VideoColorRange::Unknown;
};

struct VideoPlaneView
{
    const std::uint8_t *data = nullptr;
    std::ptrdiff_t stride = 0;
    int rowBytes = 0;
    int rows = 0;
};

/**
 * @brief Immutable, reference-counted decoded video frame.
 *
 * Plane pointers remain valid for the complete lifetime of the VideoFrame value.
 * The concrete owner is intentionally type-erased so FFmpeg and platform decoder
 * types do not leak into renderer or UI headers.
 */
class VideoFrame final
{
public:
    static constexpr int kMaximumPlanes = 3;

    VideoFrame() = default;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] VideoPixelFormat pixelFormat() const noexcept { return pixelFormat_; }
    [[nodiscard]] int planeCount() const noexcept { return planeCount_; }
    [[nodiscard]] VideoPlaneView plane(int index) const noexcept;
    [[nodiscard]] qint64 pts() const noexcept { return pts_; }
    [[nodiscard]] qint64 duration() const noexcept { return duration_; }
    [[nodiscard]] VideoRational timeBase() const noexcept { return timeBase_; }
    [[nodiscard]] const VideoColorDescription &color() const noexcept { return color_; }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }
    [[nodiscard]] std::uint64_t sessionGeneration() const noexcept
    {
        return sessionGeneration_;
    }
    [[nodiscard]] qint64 receivedMonotonicMs() const noexcept
    {
        return receivedMonotonicMs_;
    }
    [[nodiscard]] qint64 sourceTimestampMs() const noexcept
    {
        return sourceTimestampMs_;
    }
    [[nodiscard]] std::optional<std::uint32_t> sourceSequence() const noexcept
    {
        return sourceSequence_;
    }

    /**
     * @brief Copy caller-provided planes into tightly packed owned storage.
     *
     * This is used for tests, non-reference-counted decoder output and unusual
     * negative/invalid source strides. Normal FFmpeg frames use a zero-copy AVFrame
     * reference through the private adapter constructor.
     */
    static std::optional<VideoFrame> copyFromPlanes(
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
        qint64 sourceTimestampMs = -1,
        std::optional<std::uint32_t> sourceSequence = std::nullopt
    );

private:
    friend struct FfmpegVideoFrameAdapter;

    VideoFrame(
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
        qint64 sourceTimestampMs,
        std::optional<std::uint32_t> sourceSequence
    );

    int width_ = 0;
    int height_ = 0;
    VideoPixelFormat pixelFormat_ = VideoPixelFormat::Yuv420P8;
    int planeCount_ = 0;
    std::array<VideoPlaneView, kMaximumPlanes> planes_ {};
    std::shared_ptr<const void> owner_;
    qint64 pts_ = 0;
    qint64 duration_ = 0;
    VideoRational timeBase_;
    VideoColorDescription color_;
    std::uint64_t sequence_ = 0;
    std::uint64_t sessionGeneration_ = 0;
    qint64 receivedMonotonicMs_ = 0;
    qint64 sourceTimestampMs_ = -1;
    std::optional<std::uint32_t> sourceSequence_;
};

/** @brief Resolve missing matrix/range metadata using the documented SD/HD policy. */
[[nodiscard]] VideoColorDescription resolvedVideoColorDescription(
    const VideoColorDescription &source,
    int width,
    int height
) noexcept;

[[nodiscard]] bool isSupportedSdrTransfer(VideoTransferFunction transfer) noexcept;
