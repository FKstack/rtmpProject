#pragma once

#include "h264/H264MediaContracts.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rtmp_monitor::webrtc_transport::detail {

enum class H264ReceivePipelineStatus {
    Ready,
    WaitingForKeyframe,
    InvalidAccessUnit,
};

struct H264ReceivePipelineResult
{
    H264ReceivePipelineStatus status =
        H264ReceivePipelineStatus::InvalidAccessUnit;
    std::optional<SessionMediaSample> sample;
};

/**
 * Applies the transport-owned recovery policy to complete Annex-B access units.
 * RFC 6184 reassembly remains owned by libdatachannel.
 */
class H264ReceivePipeline final
{
public:
    static constexpr std::size_t kMaximumAccessUnitBytes =
        4U * 1024U * 1024U;

    [[nodiscard]] H264ReceivePipelineResult process(
        std::uint64_t generation,
        std::vector<std::uint8_t> annexB,
        std::uint32_t rtpTimestamp
    );

    void resetGeneration(std::uint64_t generation) noexcept;
    void resetRecovery() noexcept;
    [[nodiscard]] bool waitingForKeyframe() const noexcept;

private:
    [[nodiscard]] std::optional<std::int64_t> normalizeTimestamp(
        std::uint32_t rtpTimestamp
    ) noexcept;

    std::uint64_t generation_ = 0;
    std::vector<std::uint8_t> sps_;
    std::vector<std::uint8_t> pps_;
    bool waitingForKeyframe_ = true;
    bool hasTimestamp_ = false;
    std::uint32_t lastRtpTimestamp_ = 0;
    std::uint64_t unwrappedTimestamp_ = 0;
};

} // namespace rtmp_monitor::webrtc_transport::detail
