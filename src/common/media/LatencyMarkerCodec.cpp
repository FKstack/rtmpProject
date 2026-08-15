#include "media/LatencyMarkerCodec.h"

#include <QDateTime>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr double kTimestampRowY = 30.0;
constexpr double kSequenceRowY = 72.0;
constexpr double kMarkerStartX = 20.0;
constexpr double kMarkerPitch = 29.0;
constexpr double kMarkerCenterOffset = 13.0;
constexpr int kPayloadBits = 32;
constexpr int kCrcBits = 8;
constexpr int kTotalBits = kPayloadBits + kCrcBits;

struct Geometry
{
    double startX;
    double pitch;
    double centerOffset;
};

std::optional<std::uint32_t> decodePayload(
    const LatencyMarkerLumaView &view,
    double rowY,
    const Geometry &geometry
) noexcept
{
    const int y = std::clamp(
        qRound((rowY / LatencyMarkerCodec::kReferenceHeight) * view.height),
        0,
        view.height - 1
    );
    const std::uint8_t *row = view.data +
                              static_cast<std::ptrdiff_t>(y) * view.stride;
    const auto sampleBit = [&view, row, &geometry](int bitIndex) {
        const int x = std::clamp(
            qRound(
                ((geometry.startX + bitIndex * geometry.pitch +
                  geometry.centerOffset) /
                 LatencyMarkerCodec::kReferenceWidth) *
                view.width
            ),
            0,
            view.width - 1
        );
        return row[x] >= 160;
    };

    std::uint32_t payload = 0;
    for (int bit = 0; bit < kPayloadBits; ++bit) {
        payload = static_cast<std::uint32_t>(
            (payload << 1U) | (sampleBit(bit) ? 1U : 0U)
        );
    }
    std::uint8_t expectedCrc = 0;
    for (int bit = kPayloadBits; bit < kTotalBits; ++bit) {
        expectedCrc = static_cast<std::uint8_t>(
            (expectedCrc << 1U) | (sampleBit(bit) ? 1U : 0U)
        );
    }
    return LatencyMarkerCodec::crc8(payload) == expectedCrc
               ? std::optional<std::uint32_t>(payload)
               : std::nullopt;
}

void encodePayload(
    const MutableLatencyMarkerLumaView &view,
    double rowY,
    std::uint32_t payload
) noexcept
{
    const std::uint64_t bits =
        (static_cast<std::uint64_t>(payload) << kCrcBits) |
        LatencyMarkerCodec::crc8(payload);
    const int centerY = std::clamp(qRound(rowY), 0, view.height - 1);
    const int top = std::max(0, centerY - 10);
    const int bottom = std::min(view.height, centerY + 11);
    for (int bit = 0; bit < kTotalBits; ++bit) {
        const bool set = ((bits >> (kTotalBits - 1 - bit)) & 1U) != 0U;
        const int left = std::clamp(
            qRound(kMarkerStartX + bit * kMarkerPitch), 0, view.width
        );
        const int right = std::clamp(
            qRound(kMarkerStartX + bit * kMarkerPitch + 26.0),
            0,
            view.width
        );
        for (int y = top; y < bottom; ++y) {
            std::fill(
                view.data + static_cast<std::ptrdiff_t>(y) * view.stride + left,
                view.data + static_cast<std::ptrdiff_t>(y) * view.stride + right,
                set ? std::uint8_t {235} : std::uint8_t {16}
            );
        }
    }
}

} // namespace

std::uint8_t LatencyMarkerCodec::crc8(std::uint32_t value) noexcept
{
    std::uint8_t crc = 0;
    for (int byteIndex = 3; byteIndex >= 0; --byteIndex) {
        crc ^= static_cast<std::uint8_t>((value >> (byteIndex * 8)) & 0xffU);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) != 0U
                      ? static_cast<std::uint8_t>((crc << 1U) ^ 0x07U)
                      : static_cast<std::uint8_t>(crc << 1U);
        }
    }
    return crc;
}

bool LatencyMarkerCodec::encode(
    const MutableLatencyMarkerLumaView &view,
    qint64 sourceTimestampMs,
    std::uint32_t sourceSequence
) noexcept
{
    if (view.data == nullptr || view.width < 240 || view.height < 120 ||
        std::abs(view.stride) < view.width) {
        return false;
    }
    encodePayload(
        view,
        kTimestampRowY,
        static_cast<std::uint32_t>(sourceTimestampMs)
    );
    encodePayload(view, kSequenceRowY, sourceSequence);
    return true;
}

LatencyMarkerDecodeResult LatencyMarkerCodec::decode(
    const LatencyMarkerLumaView &view,
    qint64 referenceNowMs
) noexcept
{
    LatencyMarkerDecodeResult result;
    if (view.data == nullptr || view.width < 240 || view.height < 120 ||
        std::abs(view.stride) < view.width) {
        return result;
    }

    std::array<Geometry, 2> preferred {{
        {20.0, 29.0, 13.0},
        {20.0, 28.0, 13.0},
    }};
    auto tryGeometry = [&](const Geometry &geometry) {
        const auto timestamp = decodePayload(
            view, kTimestampRowY, geometry
        );
        if (!timestamp.has_value() || *timestamp == 0U) {
            return false;
        }
        result.timestampCrcValid = true;
        const std::uint32_t elapsed =
            static_cast<std::uint32_t>(referenceNowMs) - *timestamp;
        if (elapsed <= 10'000U) {
            result.sourceTimestampMs =
                referenceNowMs - static_cast<qint64>(elapsed);
        }
        if (const auto sequence = decodePayload(
                view, kSequenceRowY, geometry
            ); sequence.has_value() && *sequence != 0U) {
            result.sequenceCrcValid = true;
            result.sourceSequence = sequence;
        }
        return true;
    };

    for (const Geometry &geometry : preferred) {
        if (tryGeometry(geometry)) {
            return result;
        }
    }
    for (int pitch = 20; pitch <= 32; ++pitch) {
        for (int start = 12; start <= 24; ++start) {
            if (tryGeometry({
                    static_cast<double>(start),
                    static_cast<double>(pitch),
                    pitch / 2.0
                })) {
                return result;
            }
        }
    }
    return result;
}

LatencyMarkerDecodeResult LatencyMarkerCodec::decode(
    const LatencyMarkerLumaView &view
) noexcept
{
    return decode(view, QDateTime::currentMSecsSinceEpoch());
}
