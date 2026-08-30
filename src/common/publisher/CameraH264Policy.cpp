#include "publisher/CameraH264Policy.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <mfapi.h>
#endif

namespace rtmp_monitor::publisher::camera_detail {
namespace {
constexpr std::size_t kMaximumAccessUnitBytes = 4U * 1024U * 1024U;

#ifdef _WIN32
class MfPreflightScope final
{
public:
    MfPreflightScope() noexcept
    {
        const HRESULT initialized = CoInitializeEx(
            nullptr, COINIT_MULTITHREADED
        );
        uninitialize_ = SUCCEEDED(initialized);
        started_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~MfPreflightScope()
    {
        if (started_) MFShutdown();
        if (uninitialize_) CoUninitialize();
    }
    [[nodiscard]] bool ok() const noexcept { return started_; }
private:
    bool uninitialize_ = false;
    bool started_ = false;
};
#endif

struct NalUnit { int type = 0; std::size_t begin = 0; std::size_t end = 0; };

std::vector<NalUnit> splitAnnexB(const std::vector<std::uint8_t> &bytes)
{
    std::vector<std::size_t> starts;
    for (std::size_t index = 0; index + 3 < bytes.size(); ++index) {
        const bool three = bytes[index] == 0 && bytes[index + 1] == 0 &&
            bytes[index + 2] == 1;
        const bool four = !three && index + 4 <= bytes.size() &&
            bytes[index] == 0 && bytes[index + 1] == 0 &&
            bytes[index + 2] == 0 && bytes[index + 3] == 1;
        if (three || four) {
            starts.push_back(index);
            index += four ? 3U : 2U;
        }
    }
    std::vector<NalUnit> result;
    for (std::size_t item = 0; item < starts.size(); ++item) {
        const std::size_t begin = starts[item];
        const std::size_t end = item + 1 < starts.size()
            ? starts[item + 1] : bytes.size();
        const std::size_t prefix = begin + 3 < bytes.size() &&
            bytes[begin + 2] == 0 && bytes[begin + 3] == 1 ? 4U : 3U;
        const std::size_t payload = begin + prefix;
        if (payload < end) result.push_back(
            NalUnit {bytes[payload] & 0x1f, begin, end}
        );
    }
    return result;
}

bool hasTypes(const std::vector<std::uint8_t> &bytes,
              bool *sps, bool *pps, bool *idr)
{
    *sps = *pps = *idr = false;
    for (const NalUnit &nal : splitAnnexB(bytes)) {
        *sps = *sps || nal.type == 7;
        *pps = *pps || nal.type == 8;
        *idr = *idr || nal.type == 5;
    }
    return *sps || *pps || *idr;
}

std::vector<std::uint8_t> rbsp(
    const std::vector<std::uint8_t> &bytes,
    std::size_t begin,
    std::size_t end
)
{
    std::vector<std::uint8_t> result;
    result.reserve(end - begin);
    int zeros = 0;
    for (std::size_t index = begin; index < end; ++index) {
        const std::uint8_t value = bytes[index];
        if (zeros >= 2 && value == 3) { zeros = 0; continue; }
        result.push_back(value);
        zeros = value == 0 ? zeros + 1 : 0;
    }
    return result;
}

bool readUnsignedExpGolomb(
    const std::vector<std::uint8_t> &bytes,
    std::size_t *bit,
    std::uint32_t *value
)
{
    std::size_t zeros = 0;
    while (*bit < bytes.size() * 8U) {
        const bool one = (bytes[*bit / 8U] &
            (0x80U >> (*bit % 8U))) != 0;
        ++*bit;
        if (one) break;
        ++zeros;
        if (zeros > 31U) return false;
    }
    if (*bit > bytes.size() * 8U) return false;
    std::uint32_t suffix = 0;
    for (std::size_t index = 0; index < zeros; ++index) {
        if (*bit >= bytes.size() * 8U) return false;
        suffix = (suffix << 1U) | ((bytes[*bit / 8U] >>
            (7U - (*bit % 8U))) & 1U);
        ++*bit;
    }
    *value = ((1U << zeros) - 1U) + suffix;
    return true;
}
} // namespace

void NativeH264Preflight::observe(
    const std::vector<std::uint8_t> &annexB,
    int frameIndex
) noexcept
{
    for (const NalUnit &nal : splitAnnexB(annexB)) {
        const std::size_t prefix = nal.begin + 3 < annexB.size() &&
            annexB[nal.begin + 2] == 0 && annexB[nal.begin + 3] == 1
                ? 4U : 3U;
        const std::size_t payload = nal.begin + prefix;
        if (nal.type == 7 && payload + 3 < nal.end) {
            evidence_.hasSps = true;
            evidence_.profileIdc = annexB[payload + 1];
            evidence_.profileIop = annexB[payload + 2];
            evidence_.levelIdc = annexB[payload + 3];
        } else if (nal.type == 8) {
            evidence_.hasPps = true;
        } else if (nal.type == 5) {
            if (previousIdrFrame_ >= 0) {
                evidence_.maximumIdrGapFrames = std::max(
                    evidence_.maximumIdrGapFrames,
                    frameIndex - previousIdrFrame_
                );
            }
            previousIdrFrame_ = frameIndex;
        } else if (nal.type == 1 && payload + 1 < nal.end) {
            const std::vector<std::uint8_t> slice = rbsp(
                annexB, payload + 1, nal.end
            );
            std::size_t bit = 0;
            std::uint32_t firstMb = 0, sliceType = 0;
            if (readUnsignedExpGolomb(slice, &bit, &firstMb) &&
                readUnsignedExpGolomb(slice, &bit, &sliceType) &&
                (sliceType % 5U) == 1U) {
                evidence_.hasBFrames = true;
            }
        }
    }
}

CapturePath chooseCapturePath(
    const NativeH264Evidence &native,
    bool h264MfSyntheticPreflightPassed
) noexcept
{
    const bool nativeCompliant = native.profileIdc == 66 &&
        (native.profileIop & 0xe0) == 0xe0 &&
        native.levelIdc > 0 && native.levelIdc <= 31 &&
        !native.hasBFrames && native.hasSps && native.hasPps &&
        native.maximumIdrGapFrames > 0 &&
        native.maximumIdrGapFrames <= 30;
    if (nativeCompliant) return CapturePath::NativeH264;
    return h264MfSyntheticPreflightPassed ? CapturePath::MfEncodedNv12
                                         : CapturePath::None;
}

std::int64_t TimestampNormalizer::next(
    std::optional<std::int64_t> deviceTimestampUs
) noexcept
{
    if (deviceTimestampUs.has_value() && !origin_.has_value()) {
        origin_ = *deviceTimestampUs;
        last_ = 0;
        return 0;
    }
    if (deviceTimestampUs.has_value() && origin_.has_value()) {
        const std::int64_t normalized = *deviceTimestampUs - *origin_;
        if (normalized > last_) {
            last_ = normalized;
            return last_;
        }
    }
    last_ = std::max<std::int64_t>(0, last_ + 33'333);
    return last_;
}

std::optional<H264AccessUnit> AnnexBRecoveryPolicy::process(
    std::vector<std::uint8_t> annexB,
    std::int64_t timestampUs
)
{
    if (annexB.empty() || annexB.size() > kMaximumAccessUnitBytes) {
        waitingForIdr_ = true;
        return std::nullopt;
    }
    bool hasSps = false;
    bool hasPps = false;
    bool hasIdr = false;
    hasTypes(annexB, &hasSps, &hasPps, &hasIdr);
    for (const NalUnit &nal : splitAnnexB(annexB)) {
        if (nal.type == 7) sps_.assign(annexB.begin() + nal.begin,
                                      annexB.begin() + nal.end);
        if (nal.type == 8) pps_.assign(annexB.begin() + nal.begin,
                                      annexB.begin() + nal.end);
    }
    if (waitingForIdr_ && !hasIdr) return std::nullopt;
    if (hasIdr && (!hasSps || !hasPps)) {
        if (sps_.empty() || pps_.empty() ||
            annexB.size() + sps_.size() + pps_.size() >
                kMaximumAccessUnitBytes) {
            waitingForIdr_ = true;
            return std::nullopt;
        }
        std::vector<std::uint8_t> recovered;
        recovered.reserve(sps_.size() + pps_.size() + annexB.size());
        recovered.insert(recovered.end(), sps_.begin(), sps_.end());
        recovered.insert(recovered.end(), pps_.begin(), pps_.end());
        recovered.insert(recovered.end(), annexB.begin(), annexB.end());
        annexB = std::move(recovered);
    }
    if (hasIdr) waitingForIdr_ = false;
    H264AccessUnit result;
    result.annexB = std::move(annexB);
    result.mediaTimestampUs = timestampUs;
    result.keyFrame = hasIdr;
    return result;
}

bool validateH264MfSynthetic() noexcept
{
#ifdef _WIN32
    MfPreflightScope mfScope;
    if (!mfScope.ok()) return false;
#endif
    const AVCodec *encoder = avcodec_find_encoder_by_name("h264_mf");
    const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!encoder || !decoder) return false;
    AVCodecContext *encode = avcodec_alloc_context3(encoder);
    AVCodecContext *decode = avcodec_alloc_context3(decoder);
    AVFrame *input = av_frame_alloc();
    AVFrame *output = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    bool passed = false;
    if (!encode || !decode || !input || !output || !packet) goto cleanup;
    encode->width = 1280;
    encode->height = 720;
    encode->pix_fmt = AV_PIX_FMT_NV12;
    encode->time_base = AVRational {1, 30};
    encode->framerate = AVRational {30, 1};
    encode->gop_size = 30;
    encode->max_b_frames = 0;
    encode->flags |= AV_CODEC_FLAG_LOW_DELAY;
    av_opt_set(encode->priv_data, "profile", "baseline", 0);
    av_opt_set(encode->priv_data, "level", "3.1", 0);
    if (avcodec_open2(encode, encoder, nullptr) < 0 ||
        avcodec_open2(decode, decoder, nullptr) < 0) goto cleanup;
    input->format = encode->pix_fmt;
    input->width = encode->width;
    input->height = encode->height;
    if (av_frame_get_buffer(input, 32) < 0 ||
        av_frame_make_writable(input) < 0) goto cleanup;
    for (int row = 0; row < input->height; ++row) {
        std::memset(input->data[0] + row * input->linesize[0], 16,
                    static_cast<std::size_t>(input->width));
    }
    for (int row = 0; row < input->height / 2; ++row) {
        std::memset(input->data[1] + row * input->linesize[1], 128,
                    static_cast<std::size_t>(input->width));
    }
    {
        NativeH264Preflight preflight;
        bool firstDecoded = false;
        int packetIndex = 0;
        for (int frameIndex = 0; frameIndex <= 30; ++frameIndex) {
            input->pts = frameIndex;
            if (avcodec_send_frame(encode, input) < 0) goto cleanup;
            while (avcodec_receive_packet(encode, packet) == 0) {
                std::vector<std::uint8_t> bytes(
                    packet->data, packet->data + packet->size
                );
                preflight.observe(bytes, packetIndex++);
                if (!firstDecoded) {
                    bool sps = false, pps = false, idr = false;
                    if (!hasTypes(bytes, &sps, &pps, &idr) ||
                        !sps || !pps || !idr ||
                        avcodec_send_packet(decode, packet) < 0 ||
                        avcodec_receive_frame(decode, output) < 0) {
                        goto cleanup;
                    }
                    firstDecoded = output->width == 1280 &&
                        output->height == 720;
                }
                av_packet_unref(packet);
            }
        }
        if (avcodec_send_frame(encode, nullptr) < 0) goto cleanup;
        for (;;) {
            const int drained = avcodec_receive_packet(encode, packet);
            if (drained == AVERROR_EOF || drained == AVERROR(EAGAIN)) break;
            if (drained < 0) goto cleanup;
            std::vector<std::uint8_t> bytes(
                packet->data, packet->data + packet->size
            );
            preflight.observe(bytes, packetIndex++);
            if (!firstDecoded) {
                bool sps = false, pps = false, idr = false;
                if (!hasTypes(bytes, &sps, &pps, &idr) ||
                    !sps || !pps || !idr ||
                    avcodec_send_packet(decode, packet) < 0 ||
                    avcodec_receive_frame(decode, output) < 0) {
                    goto cleanup;
                }
                firstDecoded = output->width == 1280 &&
                    output->height == 720;
            }
            av_packet_unref(packet);
        }
        const NativeH264Evidence evidence = preflight.evidence();
        passed = firstDecoded && encode->max_b_frames == 0 &&
            chooseCapturePath(evidence, false) == CapturePath::NativeH264;
    }
cleanup:
    av_packet_free(&packet);
    av_frame_free(&output);
    av_frame_free(&input);
    avcodec_free_context(&decode);
    avcodec_free_context(&encode);
    return passed;
}

} // namespace rtmp_monitor::publisher::camera_detail
