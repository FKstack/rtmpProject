#pragma once

#include "h264/H264MediaContracts.h"
#include "publisher/Mp4H264PublisherSource.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace rtmp_monitor::publisher { class CameraH264PublisherSource; }
namespace rtmp_monitor::publisher::camera_detail {

using CameraWorkerTestRun = std::function<PublisherSourceError(
    std::uint32_t,
    const PublisherSubmitCallback &,
    const std::function<bool()> &
)>;
using CameraWorkerTestInterrupt = std::function<void()>;

/** Private component seam; production callers cannot include this header. */
class CameraSourceTestAccess final
{
public:
    [[nodiscard]] static std::unique_ptr<CameraH264PublisherSource> create(
        CameraWorkerTestRun run,
        CameraWorkerTestInterrupt interrupt
    );
};

enum class CapturePath { None, NativeH264, MfEncodedNv12 };

struct NativeH264Evidence
{
    int profileIdc = 0;
    int profileIop = 0;
    int levelIdc = 0;
    bool hasBFrames = true;
    bool hasSps = false;
    bool hasPps = false;
    int maximumIdrGapFrames = 0;
};

class NativeH264Preflight final
{
public:
    void observe(
        const std::vector<std::uint8_t> &annexB,
        int frameIndex
    ) noexcept;
    [[nodiscard]] NativeH264Evidence evidence() const noexcept
    { return evidence_; }
private:
    NativeH264Evidence evidence_ {0, 0, 0, false, false, false, 0};
    int previousIdrFrame_ = -1;
};

[[nodiscard]] CapturePath chooseCapturePath(
    const NativeH264Evidence &native,
    bool h264MfSyntheticPreflightPassed
) noexcept;

class TimestampNormalizer final
{
public:
    [[nodiscard]] std::int64_t next(
        std::optional<std::int64_t> deviceTimestampUs
    ) noexcept;
private:
    std::optional<std::int64_t> origin_;
    std::int64_t last_ = -33'333;
};

class AnnexBRecoveryPolicy final
{
public:
    [[nodiscard]] std::optional<H264AccessUnit> process(
        std::vector<std::uint8_t> annexB,
        std::int64_t timestampUs
    );
    void requireRecoveryIdr() noexcept { waitingForIdr_ = true; }
private:
    std::vector<std::uint8_t> sps_;
    std::vector<std::uint8_t> pps_;
    bool waitingForIdr_ = true;
};

/** Actually opens h264_mf, encodes synthetic NV12, and decodes its first AU. */
[[nodiscard]] bool validateH264MfSynthetic() noexcept;

} // namespace rtmp_monitor::publisher::camera_detail
