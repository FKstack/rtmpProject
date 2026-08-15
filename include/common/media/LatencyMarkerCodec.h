#pragma once

#include <QtGlobal>

#include <cstdint>
#include <optional>

struct LatencyMarkerLumaView
{
    const std::uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    std::ptrdiff_t stride = 0;
};

struct MutableLatencyMarkerLumaView
{
    std::uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    std::ptrdiff_t stride = 0;
};

struct LatencyMarkerDecodeResult
{
    std::optional<qint64> sourceTimestampMs;
    std::optional<std::uint32_t> sourceSequence;
    bool timestampCrcValid = false;
    bool sequenceCrcValid = false;
};

/**
 * Machine-readable two-row marker used by camera qualification tests.
 *
 * Row one preserves the historical 32-bit epoch-millisecond payload plus
 * CRC-8. Row two uses the same layout for a 32-bit source-frame sequence.
 */
class LatencyMarkerCodec final
{
public:
    static constexpr int kReferenceWidth = 1280;
    static constexpr int kReferenceHeight = 720;

    [[nodiscard]] static std::uint8_t crc8(std::uint32_t value) noexcept;

    [[nodiscard]] static bool encode(
        const MutableLatencyMarkerLumaView &view,
        qint64 sourceTimestampMs,
        std::uint32_t sourceSequence
    ) noexcept;

    [[nodiscard]] static LatencyMarkerDecodeResult decode(
        const LatencyMarkerLumaView &view,
        qint64 referenceNowMs
    ) noexcept;

    [[nodiscard]] static LatencyMarkerDecodeResult decode(
        const LatencyMarkerLumaView &view
    ) noexcept;
};
