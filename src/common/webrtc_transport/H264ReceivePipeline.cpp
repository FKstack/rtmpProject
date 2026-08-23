#include "webrtc_transport/H264ReceivePipeline.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace rtmp_monitor::webrtc_transport::detail {
namespace {

constexpr std::uint64_t kRtpClockRate = 90'000U;
constexpr std::uint64_t kMicrosecondsPerSecond = 1'000'000U;

struct NalUnitView
{
    std::size_t startCodeOffset = 0;
    std::size_t payloadOffset = 0;
    std::size_t endOffset = 0;
    std::uint8_t type = 0;
};

std::size_t startCodeLength(
    const std::vector<std::uint8_t> &bytes,
    std::size_t offset
) noexcept
{
    if (offset + 3 <= bytes.size() && bytes[offset] == 0 &&
        bytes[offset + 1] == 0 && bytes[offset + 2] == 1) {
        return 3;
    }
    if (offset + 4 <= bytes.size() && bytes[offset] == 0 &&
        bytes[offset + 1] == 0 && bytes[offset + 2] == 0 &&
        bytes[offset + 3] == 1) {
        return 4;
    }
    return 0;
}

std::optional<std::vector<NalUnitView>> parseNalUnits(
    const std::vector<std::uint8_t> &bytes
)
{
    if (bytes.empty() || startCodeLength(bytes, 0) == 0) {
        return std::nullopt;
    }

    std::vector<NalUnitView> units;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t prefix = startCodeLength(bytes, offset);
        if (prefix == 0 || offset + prefix >= bytes.size()) {
            return std::nullopt;
        }
        const std::size_t payload = offset + prefix;
        std::size_t end = payload + 1;
        while (end < bytes.size() && startCodeLength(bytes, end) == 0) {
            ++end;
        }
        if (end == payload || (bytes[payload] & 0x1fU) == 0) {
            return std::nullopt;
        }
        units.push_back(
            {offset, payload, end,
             static_cast<std::uint8_t>(bytes[payload] & 0x1fU)}
        );
        offset = end;
    }
    return units.empty()
               ? std::nullopt
               : std::optional<std::vector<NalUnitView>>(std::move(units));
}

std::vector<std::uint8_t> canonicalNal(
    const std::vector<std::uint8_t> &bytes,
    const NalUnitView &unit
)
{
    std::vector<std::uint8_t> result {0, 0, 0, 1};
    result.insert(
        result.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(unit.payloadOffset),
        bytes.begin() + static_cast<std::ptrdiff_t>(unit.endOffset)
    );
    return result;
}

} // namespace

H264ReceivePipelineResult H264ReceivePipeline::process(
    std::uint64_t generation,
    std::vector<std::uint8_t> annexB,
    std::uint32_t rtpTimestamp
)
{
    if (generation_ != generation) {
        resetGeneration(generation);
    }
    if (annexB.empty() || annexB.size() > kMaximumAccessUnitBytes) {
        resetRecovery();
        return {H264ReceivePipelineStatus::InvalidAccessUnit, std::nullopt};
    }

    const auto units = parseNalUnits(annexB);
    if (!units.has_value()) {
        resetRecovery();
        return {H264ReceivePipelineStatus::InvalidAccessUnit, std::nullopt};
    }

    bool hasSps = false;
    bool hasPps = false;
    bool hasIdr = false;
    for (const NalUnitView &unit : *units) {
        if (unit.type == 7) {
            sps_ = canonicalNal(annexB, unit);
            hasSps = true;
        } else if (unit.type == 8) {
            pps_ = canonicalNal(annexB, unit);
            hasPps = true;
        } else if (unit.type == 5) {
            hasIdr = true;
        }
    }

    const auto timestampUs = normalizeTimestamp(rtpTimestamp);
    if (!timestampUs.has_value()) {
        resetRecovery();
        return {H264ReceivePipelineStatus::InvalidAccessUnit, std::nullopt};
    }

    if (waitingForKeyframe_) {
        if (!hasIdr || sps_.empty() || pps_.empty()) {
            return {H264ReceivePipelineStatus::WaitingForKeyframe, std::nullopt};
        }
        std::vector<std::uint8_t> recovery;
        const std::size_t prefixBytes =
            (hasSps ? 0 : sps_.size()) + (hasPps ? 0 : pps_.size());
        if (annexB.size() + prefixBytes > kMaximumAccessUnitBytes) {
            resetRecovery();
            return {H264ReceivePipelineStatus::InvalidAccessUnit, std::nullopt};
        }
        recovery.reserve(annexB.size() + prefixBytes);
        if (!hasSps) recovery.insert(recovery.end(), sps_.begin(), sps_.end());
        if (!hasPps) recovery.insert(recovery.end(), pps_.begin(), pps_.end());
        recovery.insert(recovery.end(), annexB.begin(), annexB.end());
        annexB = std::move(recovery);
        waitingForKeyframe_ = false;
    }

    SessionMediaSample sample;
    sample.generation = generation;
    sample.accessUnit.annexB = std::move(annexB);
    sample.accessUnit.mediaTimestampUs = *timestampUs;
    sample.accessUnit.keyFrame = hasIdr;
    return {
        H264ReceivePipelineStatus::Ready,
        std::optional<SessionMediaSample>(std::move(sample))
    };
}

void H264ReceivePipeline::resetGeneration(std::uint64_t generation) noexcept
{
    generation_ = generation;
    resetRecovery();
    hasTimestamp_ = false;
    lastRtpTimestamp_ = 0;
    unwrappedTimestamp_ = 0;
}

void H264ReceivePipeline::resetRecovery() noexcept
{
    sps_.clear();
    pps_.clear();
    waitingForKeyframe_ = true;
}

bool H264ReceivePipeline::waitingForKeyframe() const noexcept
{
    return waitingForKeyframe_;
}

std::optional<std::int64_t> H264ReceivePipeline::normalizeTimestamp(
    std::uint32_t rtpTimestamp
) noexcept
{
    if (!hasTimestamp_) {
        hasTimestamp_ = true;
        lastRtpTimestamp_ = rtpTimestamp;
        unwrappedTimestamp_ = 0;
        return 0;
    }

    const std::uint32_t delta = rtpTimestamp - lastRtpTimestamp_;
    if (delta > std::numeric_limits<std::uint32_t>::max() / 2U) {
        return std::nullopt;
    }
    lastRtpTimestamp_ = rtpTimestamp;
    unwrappedTimestamp_ += delta;
    const std::uint64_t microseconds =
        (unwrappedTimestamp_ * kMicrosecondsPerSecond) / kRtpClockRate;
    if (microseconds >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(microseconds);
}

} // namespace rtmp_monitor::webrtc_transport::detail
