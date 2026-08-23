#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @brief Protocol-neutral encoded H.264 access unit.
 *
 * The payload uses Annex-B start codes. The timestamp is a media timestamp in
 * microseconds, not a wall clock or an RTP timestamp. Session identity and
 * transport diagnostics intentionally stay outside this reusable source value.
 */
struct H264AccessUnit
{
    std::vector<std::uint8_t> annexB;
    std::int64_t mediaTimestampUs = 0;
    bool keyFrame = false;
};

/** @brief Session envelope used when a transport submits an encoded AU. */
struct SessionMediaSample
{
    std::uint64_t generation = 0;
    H264AccessUnit accessUnit;
};

/** @brief Observable result for bounded encoded-video source/sink ports. */
enum class H264SubmitResult {
    Accepted,
    AcceptedAfterDrop,
    DroppedCapacity,
    DroppedUntilKeyframe,
    Closed,
    InvalidGeneration,
    InvalidAccessUnit,
    ResourceFailure,
};

/**
 * @brief Checks the protocol-neutral AU invariants without parsing the codec.
 *
 * The decoder owns codec-level validation. This check only guarantees a
 * non-empty Annex-B payload, a non-negative media timestamp and the caller's
 * explicit size bound.
 */
[[nodiscard]] inline bool isValidH264AccessUnit(
    const H264AccessUnit &accessUnit,
    std::size_t maximumBytes
) noexcept
{
    if (accessUnit.annexB.empty() || maximumBytes == 0 ||
        accessUnit.annexB.size() > maximumBytes ||
        accessUnit.mediaTimestampUs < 0) {
        return false;
    }

    const auto &bytes = accessUnit.annexB;
    const bool threeByteStartCode =
        bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1;
    const bool fourByteStartCode =
        bytes.size() >= 5 && bytes[0] == 0 && bytes[1] == 0 &&
        bytes[2] == 0 && bytes[3] == 1;
    return threeByteStartCode || fourByteStartCode;
}
