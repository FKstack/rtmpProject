#include "publisher/CameraH264PublisherSource.h"
#include "publisher/CameraH264Policy.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
}

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

namespace rtmp_monitor::publisher {
namespace {

#ifdef _WIN32
class MfScope final
{
public:
    MfScope() noexcept
    {
        const HRESULT apartment = CoInitializeEx(
            nullptr, COINIT_MULTITHREADED
        );
        uninitialize_ = SUCCEEDED(apartment);
        started_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~MfScope()
    {
        if (started_) MFShutdown();
        if (uninitialize_) CoUninitialize();
    }
    [[nodiscard]] bool ok() const noexcept { return started_; }
private:
    bool uninitialize_ = false;
    bool started_ = false;
};

PublisherSourceError enumerate(std::vector<CameraDeviceInfo> *devices) noexcept
{
    if (!devices) return PublisherSourceError::InvalidState;
    devices->clear();
    MfScope scope;
    if (!scope.ok()) return PublisherSourceError::OpenFailed;
    IMFAttributes *attributes = nullptr;
    IMFActivate **activations = nullptr;
    UINT32 count = 0;
    HRESULT result = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(result)) {
        result = attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );
    }
    if (SUCCEEDED(result)) {
        result = MFEnumDeviceSources(attributes, &activations, &count);
    }
    if (attributes) attributes->Release();
    if (FAILED(result)) {
        if (activations) {
            for (UINT32 index = 0; index < count; ++index) {
                if (activations[index]) activations[index]->Release();
            }
            CoTaskMemFree(activations);
        }
        return PublisherSourceError::OpenFailed;
    }
    try {
        devices->reserve(count);
        for (UINT32 index = 0; index < count; ++index) {
            devices->push_back(CameraDeviceInfo {
                index,
                std::string("camera-") + std::to_string(index + 1)
            });
        }
    } catch (...) {
        for (UINT32 index = 0; index < count; ++index) {
            if (activations[index]) activations[index]->Release();
        }
        CoTaskMemFree(activations);
        devices->clear();
        return PublisherSourceError::OpenFailed;
    }
    for (UINT32 index = 0; index < count; ++index) {
        if (activations[index]) activations[index]->Release();
    }
    CoTaskMemFree(activations);
    return PublisherSourceError::None;
}

struct NativeCandidate
{
    IMFMediaType *type = nullptr;
    std::vector<std::uint8_t> parameterSets;
    int nalLengthBytes = 4;
};

void releaseNative(NativeCandidate *candidate) noexcept
{
    if (candidate->type) candidate->type->Release();
    candidate->type = nullptr;
    candidate->parameterSets.clear();
    candidate->nalLengthBytes = 4;
}

bool appendAvccParameterSets(
    const std::vector<std::uint8_t> &extra,
    NativeCandidate *candidate
)
{
    if (extra.size() < 7U || extra[0] != 1U) return false;
    candidate->nalLengthBytes = (extra[4] & 3U) + 1;
    std::size_t offset = 5;
    const int spsCount = extra[offset++] & 31U;
    auto appendSets = [&](int count) {
        for (int item = 0; item < count; ++item) {
            if (offset + 2U > extra.size()) return false;
            const std::size_t length =
                (static_cast<std::size_t>(extra[offset]) << 8U) |
                extra[offset + 1];
            offset += 2;
            if (offset + length > extra.size()) return false;
            candidate->parameterSets.insert(
                candidate->parameterSets.end(), {0, 0, 0, 1}
            );
            candidate->parameterSets.insert(
                candidate->parameterSets.end(),
                extra.begin() + static_cast<std::ptrdiff_t>(offset),
                extra.begin() + static_cast<std::ptrdiff_t>(offset + length)
            );
            offset += length;
        }
        return true;
    };
    if (!appendSets(spsCount) || offset >= extra.size()) return false;
    return appendSets(extra[offset++]);
}

NativeCandidate findNativeH264(IMFSourceReader *reader)
{
    NativeCandidate candidate;
    for (DWORD index = 0; ; ++index) {
        IMFMediaType *type = nullptr;
        const HRESULT status = reader->GetNativeMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            index, &type
        );
        if (status != S_OK) break;
        if (!type) continue;
        GUID subtype {};
        UINT32 width = 0, height = 0, numerator = 0, denominator = 0;
        const bool matches = SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
            subtype == MFVideoFormat_H264 &&
            SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) &&
            width == 1280 && height == 720 &&
            SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE,
                                         &numerator, &denominator)) &&
            denominator != 0 && numerator / denominator == 30;
        UINT32 blobSize = 0;
        if (matches && SUCCEEDED(type->GetBlobSize(
                MF_MT_MPEG_SEQUENCE_HEADER, &blobSize
            )) && blobSize > 0) {
            std::vector<std::uint8_t> extra(blobSize);
            UINT32 written = 0;
            if (SUCCEEDED(type->GetBlob(
                    MF_MT_MPEG_SEQUENCE_HEADER, extra.data(), blobSize,
                    &written
                ))) {
                extra.resize(written);
                candidate.type = type;
                if (extra.size() >= 4U && extra[0] == 0 && extra[1] == 0 &&
                    (extra[2] == 1 || (extra[2] == 0 && extra[3] == 1))) {
                    candidate.parameterSets = std::move(extra);
                } else if (!appendAvccParameterSets(extra, &candidate)) {
                    releaseNative(&candidate);
                    type = nullptr;
                }
                if (candidate.type) return candidate;
            }
        }
        if (type) type->Release();
    }
    return candidate;
}

PublisherSourceError sourceReaderFlagError(DWORD flags) noexcept
{
    if ((flags & MF_SOURCE_READERF_ERROR) != 0 ||
        (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
        return PublisherSourceError::DeviceLost;
    }
    if ((flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED |
                  MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) != 0) {
        return PublisherSourceError::CompatiblePathUnavailable;
    }
    return PublisherSourceError::None;
}

std::vector<std::uint8_t> toAnnexB(
    const BYTE *bytes,
    DWORD length,
    int nalLengthBytes
)
{
    if (!bytes || length < 4U) return {};
    if (bytes[0] == 0 && bytes[1] == 0 &&
        (bytes[2] == 1 || (bytes[2] == 0 && bytes[3] == 1))) {
        return {bytes, bytes + length};
    }
    std::vector<std::uint8_t> result;
    std::size_t offset = 0;
    while (offset + static_cast<std::size_t>(nalLengthBytes) <= length) {
        std::size_t nalSize = 0;
        for (int index = 0; index < nalLengthBytes; ++index) {
            nalSize = (nalSize << 8U) | bytes[offset + index];
        }
        offset += static_cast<std::size_t>(nalLengthBytes);
        if (nalSize == 0 || offset + nalSize > length) return {};
        result.insert(result.end(), {0, 0, 0, 1});
        result.insert(result.end(), bytes + offset, bytes + offset + nalSize);
        offset += nalSize;
    }
    return offset == length ? result : std::vector<std::uint8_t> {};
}
#endif

} // namespace

class CameraH264PublisherSource::Impl final
{
public:
    Impl() = default;
    Impl(
        camera_detail::CameraWorkerTestRun testRun,
        camera_detail::CameraWorkerTestInterrupt testInterrupt
    ) : testRun_(std::move(testRun)),
        testInterrupt_(std::move(testInterrupt)) {}
    ~Impl() { stop(); }

    PublisherSourceError start(
        std::uint32_t cameraIndex,
        PublisherSubmitCallback submit
    )
    {
        if (!submit || started_) return PublisherSourceError::InvalidState;
        if (testRun_) {
            started_ = true;
            {
                const std::lock_guard lock(mutex_);
                snapshot_.running = true;
            }
            worker_ = std::thread([
                this, cameraIndex, submit = std::move(submit)
            ] {
                finish(testRun_(
                    cameraIndex, submit, [this] { return stopRequested(); }
                ));
            });
            return PublisherSourceError::None;
        }
#ifndef _WIN32
        (void)cameraIndex;
        return PublisherSourceError::PlatformUnsupported;
#else
        started_ = true;
        {
            const std::lock_guard lock(mutex_);
            snapshot_.running = true;
        }
        worker_ = std::thread([this, cameraIndex, submit = std::move(submit)] {
            runCamera(cameraIndex, submit);
        });
        return PublisherSourceError::None;
#endif
    }

    PublisherSourceError waitForCompletion(std::chrono::milliseconds timeout)
    {
        {
            std::unique_lock lock(mutex_);
            if (!started_) return PublisherSourceError::InvalidState;
            if (!changed_.wait_for(lock, timeout, [this] {
                    return !snapshot_.running;
                })) {
                return PublisherSourceError::Timeout;
            }
        }
        joinWorker();
        const std::lock_guard lock(mutex_);
        return snapshot_.error;
    }

    PublisherSourceSnapshot snapshot() const noexcept
    {
        const std::lock_guard lock(mutex_);
        return snapshot_;
    }

    void stop() noexcept
    {
#ifdef _WIN32
        IMFSourceReader *reader = nullptr;
#endif
        {
            const std::lock_guard lock(mutex_);
            stopRequested_ = true;
#ifdef _WIN32
            reader = readerForStop_;
            if (reader) reader->AddRef();
#endif
            changed_.notify_all();
        }
        if (testInterrupt_) testInterrupt_();
#ifdef _WIN32
        if (reader) {
            (void)reader->Flush(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM)
            );
            reader->Release();
        }
#endif
        joinWorker();
    }

private:
    void joinWorker() noexcept
    {
        const std::lock_guard lock(joinMutex_);
        if (worker_.joinable()) worker_.join();
    }

    bool stopRequested() const noexcept
    {
        const std::lock_guard lock(mutex_);
        return stopRequested_;
    }

#ifdef _WIN32
    void installReader(IMFSourceReader *reader) noexcept
    {
        const std::lock_guard lock(mutex_);
        readerForStop_ = reader;
    }

    void releaseReader(IMFSourceReader *reader) noexcept
    {
        if (!reader) return;
        {
            const std::lock_guard lock(mutex_);
            if (readerForStop_ == reader) readerForStop_ = nullptr;
        }
        reader->Release();
    }

    void runCamera(
        std::uint32_t cameraIndex,
        const PublisherSubmitCallback &submit
    )
    {
        MfScope scope;
        if (!scope.ok()) { finish(PublisherSourceError::OpenFailed); return; }

        IMFAttributes *attributes = nullptr;
        IMFActivate **activations = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (SUCCEEDED(hr)) hr = attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );
        if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(
            attributes, &activations, &count
        );
        if (attributes) attributes->Release();
        if (FAILED(hr) || cameraIndex >= count) {
            if (activations) {
                for (UINT32 i = 0; i < count; ++i) activations[i]->Release();
                CoTaskMemFree(activations);
            }
            finish(cameraIndex >= count ? PublisherSourceError::CameraNotFound
                                        : PublisherSourceError::OpenFailed);
            return;
        }
        IMFActivate *selectedActivation = activations[cameraIndex];
        selectedActivation->AddRef();
        IMFMediaSource *mediaSource = nullptr;
        hr = selectedActivation->ActivateObject(
            IID_PPV_ARGS(&mediaSource)
        );
        for (UINT32 i = 0; i < count; ++i) activations[i]->Release();
        CoTaskMemFree(activations);
        if (FAILED(hr) || !mediaSource) {
            selectedActivation->Release();
            finish(PublisherSourceError::OpenFailed); return;
        }

        IMFSourceReader *reader = nullptr;
        hr = MFCreateSourceReaderFromMediaSource(mediaSource, nullptr, &reader);
        mediaSource->Release();
        if (FAILED(hr) || !reader) {
            (void)selectedActivation->ShutdownObject();
            selectedActivation->Release();
            finish(PublisherSourceError::OpenFailed); return;
        }
        installReader(reader);

        bool nativePreflightAttempted = false;
        NativeCandidate native = findNativeH264(reader);
        if (native.type && SUCCEEDED(reader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                nullptr, native.type
            ))) {
            nativePreflightAttempted = true;
            camera_detail::NativeH264Preflight preflight;
            camera_detail::AnnexBRecoveryPolicy recovery;
            camera_detail::TimestampNormalizer timestamps;
            bool nativeReadFailed = false;
            bool parameterSetsInjected = false;
            PublisherSourceError nativeReadError =
                PublisherSourceError::DeviceLost;
            for (int frameNumber = 0; frameNumber < 90 &&
                 !stopRequested(); ++frameNumber) {
                DWORD flags = 0;
                LONGLONG sampleTime = 0;
                IMFSample *sample = nullptr;
                hr = reader->ReadSample(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                    0, nullptr, &flags, &sampleTime, &sample
                );
                const PublisherSourceError flagError =
                    sourceReaderFlagError(flags);
                if (FAILED(hr) || flagError != PublisherSourceError::None) {
                    if (sample) sample->Release();
                    nativeReadFailed = true;
                    nativeReadError = FAILED(hr)
                        ? PublisherSourceError::DeviceLost : flagError;
                    break;
                }
                if (!sample) continue;
                IMFMediaBuffer *buffer = nullptr;
                BYTE *bytes = nullptr;
                DWORD maximum = 0, length = 0;
                hr = sample->ConvertToContiguousBuffer(&buffer);
                if (SUCCEEDED(hr)) hr = buffer->Lock(
                    &bytes, &maximum, &length
                );
                std::vector<std::uint8_t> annexB = SUCCEEDED(hr)
                    ? toAnnexB(bytes, length, native.nalLengthBytes)
                    : std::vector<std::uint8_t> {};
                if (bytes) buffer->Unlock();
                if (buffer) buffer->Release();
                sample->Release();
                if (annexB.empty()) continue;
                std::vector<std::uint8_t> observed = annexB;
                if (!parameterSetsInjected) {
                    observed.insert(observed.begin(),
                                    native.parameterSets.begin(),
                                    native.parameterSets.end());
                    parameterSetsInjected = true;
                }
                preflight.observe(observed, frameNumber);
                (void)recovery.process(
                    std::move(observed), sampleTime / 10
                );
                if (camera_detail::chooseCapturePath(
                        preflight.evidence(), false
                    ) ==
                    camera_detail::CapturePath::NativeH264) {
                    recovery.requireRecoveryIdr();
                    PublisherSourceError directError =
                        PublisherSourceError::None;
                    int directFrame = frameNumber + 1;
                    while (!stopRequested()) {
                        DWORD directFlags = 0;
                        LONGLONG directTime = 0;
                        IMFSample *directSample = nullptr;
                        hr = reader->ReadSample(
                            static_cast<DWORD>(
                                MF_SOURCE_READER_FIRST_VIDEO_STREAM
                            ), 0, nullptr, &directFlags, &directTime,
                            &directSample
                        );
                        const PublisherSourceError directFlagError =
                            sourceReaderFlagError(directFlags);
                        if (FAILED(hr) || directFlagError !=
                            PublisherSourceError::None) {
                            if (directSample) directSample->Release();
                            directError = FAILED(hr)
                                ? PublisherSourceError::DeviceLost
                                : directFlagError;
                            break;
                        }
                        if (!directSample) continue;
                        IMFMediaBuffer *directBuffer = nullptr;
                        BYTE *directBytes = nullptr;
                        DWORD directMaximum = 0, directLength = 0;
                        hr = directSample->ConvertToContiguousBuffer(
                            &directBuffer
                        );
                        if (SUCCEEDED(hr)) hr = directBuffer->Lock(
                            &directBytes, &directMaximum, &directLength
                        );
                        auto directAnnexB = SUCCEEDED(hr)
                            ? toAnnexB(directBytes, directLength,
                                      native.nalLengthBytes)
                            : std::vector<std::uint8_t> {};
                        if (directBytes) directBuffer->Unlock();
                        if (directBuffer) directBuffer->Release();
                        directSample->Release();
                        preflight.observe(directAnnexB, directFrame++);
                        auto accessUnit = recovery.process(
                            std::move(directAnnexB), 0
                        );
                        if (!accessUnit.has_value()) continue;
                        accessUnit->mediaTimestampUs =
                            timestamps.next(directTime / 10);
                        const bool keyFrame = accessUnit->keyFrame;
                        const H264SubmitResult result = submit(
                            std::move(*accessUnit)
                        );
                        const std::lock_guard lock(mutex_);
                        if (result == H264SubmitResult::Accepted ||
                            result == H264SubmitResult::AcceptedAfterDrop) {
                            ++snapshot_.emittedAccessUnits;
                            if (keyFrame) ++snapshot_.emittedKeyframes;
                        } else if (
                            result == H264SubmitResult::DroppedCapacity ||
                            result == H264SubmitResult::DroppedUntilKeyframe
                        ) {
                            ++snapshot_.droppedAccessUnits;
                            recovery.requireRecoveryIdr();
                        } else {
                            directError = PublisherSourceError::SubmitFailure;
                            break;
                        }
                    }
                    if (stopRequested()) directError = PublisherSourceError::Stopped;
                    releaseNative(&native);
                    releaseReader(reader);
                    (void)selectedActivation->ShutdownObject();
                    selectedActivation->Release();
                    finish(directError);
                    return;
                }
            }
            if (nativeReadFailed) {
                releaseNative(&native);
                releaseReader(reader);
                (void)selectedActivation->ShutdownObject();
                selectedActivation->Release();
                finish(nativeReadError);
                return;
            }
        }
        releaseNative(&native);

        if (stopRequested()) {
            releaseReader(reader);
            (void)selectedActivation->ShutdownObject();
            selectedActivation->Release();
            finish(PublisherSourceError::Stopped);
            return;
        }

        // A non-compliant native stream may leave the capture source in an
        // encoded state with queued samples. Shut it down and reopen the same
        // authorized device before selecting NV12 for the bounded MF fallback.
        if (nativePreflightAttempted) {
            releaseReader(reader);
            reader = nullptr;
            (void)selectedActivation->ShutdownObject();
            mediaSource = nullptr;
            hr = selectedActivation->ActivateObject(
                IID_PPV_ARGS(&mediaSource)
            );
            if (SUCCEEDED(hr) && mediaSource) {
                hr = MFCreateSourceReaderFromMediaSource(
                    mediaSource, nullptr, &reader
                );
            }
            if (mediaSource) mediaSource->Release();
            if (FAILED(hr) || !reader) {
                selectedActivation->Release();
                finish(PublisherSourceError::CompatiblePathUnavailable);
                return;
            }
            installReader(reader);
        }

        if (!camera_detail::validateH264MfSynthetic()) {
            releaseReader(reader);
            (void)selectedActivation->ShutdownObject();
            selectedActivation->Release();
            finish(PublisherSourceError::EncoderValidationFailed);
            return;
        }

        IMFMediaType *nv12 = nullptr;
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&nv12);
        if (SUCCEEDED(hr)) hr = nv12->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = nv12->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(nv12, MF_MT_FRAME_SIZE, 1280, 720);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(nv12, MF_MT_FRAME_RATE, 30, 1);
        if (SUCCEEDED(hr)) hr = nv12->SetUINT32(
            MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive
        );
        if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            nullptr, nv12
        );
        if (nv12) nv12->Release();
        if (FAILED(hr)) {
            releaseReader(reader);
            (void)selectedActivation->ShutdownObject();
            selectedActivation->Release();
            finish(PublisherSourceError::CompatiblePathUnavailable);
            return;
        }
        const AVCodec *codec = avcodec_find_encoder_by_name("h264_mf");
        AVCodecContext *encoder = codec ? avcodec_alloc_context3(codec) : nullptr;
        AVFrame *frame = av_frame_alloc();
        AVPacket *packet = av_packet_alloc();
        if (!encoder || !frame || !packet) hr = E_OUTOFMEMORY;
        if (SUCCEEDED(hr)) {
            encoder->width = 1280; encoder->height = 720;
            encoder->pix_fmt = AV_PIX_FMT_NV12;
            encoder->time_base = AVRational {1, 30};
            encoder->framerate = AVRational {30, 1};
            encoder->gop_size = 30; encoder->max_b_frames = 0;
            encoder->flags |= AV_CODEC_FLAG_LOW_DELAY;
            av_opt_set(encoder->priv_data, "profile", "baseline", 0);
            av_opt_set(encoder->priv_data, "level", "3.1", 0);
            if (avcodec_open2(encoder, codec, nullptr) < 0) hr = E_FAIL;
        }
        if (SUCCEEDED(hr)) {
            frame->format = AV_PIX_FMT_NV12;
            frame->width = 1280; frame->height = 720;
            if (av_frame_get_buffer(frame, 32) < 0) hr = E_OUTOFMEMORY;
        }

        camera_detail::TimestampNormalizer timestamps;
        camera_detail::AnnexBRecoveryPolicy recovery;
        std::int64_t frameIndex = 0;
        PublisherSourceError error = PublisherSourceError::None;
        while (SUCCEEDED(hr) && !stopRequested()) {
            DWORD flags = 0;
            LONGLONG sampleTime = 0;
            IMFSample *sample = nullptr;
            hr = reader->ReadSample(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                0, nullptr,
                &flags, &sampleTime, &sample
            );
            const PublisherSourceError flagError =
                sourceReaderFlagError(flags);
            if (FAILED(hr) || flagError != PublisherSourceError::None) {
                if (sample) sample->Release();
                error = FAILED(hr) ? PublisherSourceError::DeviceLost
                                   : flagError;
                break;
            }
            if (!sample) continue;
            IMFMediaBuffer *buffer = nullptr;
            BYTE *bytes = nullptr;
            DWORD maximum = 0, length = 0;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (SUCCEEDED(hr)) hr = buffer->Lock(&bytes, &maximum, &length);
            const DWORD required = 1280U * 720U * 3U / 2U;
            if (SUCCEEDED(hr) && length >= required &&
                av_frame_make_writable(frame) >= 0) {
                for (int row = 0; row < 720; ++row) {
                    std::memcpy(frame->data[0] + row * frame->linesize[0],
                                bytes + row * 1280, 1280);
                }
                const BYTE *uv = bytes + 1280 * 720;
                for (int row = 0; row < 360; ++row) {
                    std::memcpy(frame->data[1] + row * frame->linesize[1],
                                uv + row * 1280, 1280);
                }
                frame->pts = frameIndex++;
                if (avcodec_send_frame(encoder, frame) < 0) hr = E_FAIL;
                while (SUCCEEDED(hr) &&
                       avcodec_receive_packet(encoder, packet) == 0) {
                    std::vector<std::uint8_t> annexB(
                        packet->data, packet->data + packet->size
                    );
                    auto accessUnit = recovery.process(
                        std::move(annexB), 0
                    );
                    if (accessUnit.has_value()) {
                        accessUnit->mediaTimestampUs =
                            timestamps.next(sampleTime / 10);
                        const H264SubmitResult result = submit(
                            std::move(*accessUnit)
                        );
                        const std::lock_guard lock(mutex_);
                        if (result == H264SubmitResult::Accepted ||
                            result == H264SubmitResult::AcceptedAfterDrop) {
                            ++snapshot_.emittedAccessUnits;
                            if ((packet->flags & AV_PKT_FLAG_KEY) != 0)
                                ++snapshot_.emittedKeyframes;
                        } else if (result == H264SubmitResult::DroppedCapacity ||
                                   result == H264SubmitResult::DroppedUntilKeyframe) {
                            ++snapshot_.droppedAccessUnits;
                            recovery.requireRecoveryIdr();
                        } else {
                            error = PublisherSourceError::SubmitFailure;
                            hr = E_FAIL;
                        }
                    }
                    av_packet_unref(packet);
                }
            } else if (SUCCEEDED(hr)) {
                hr = E_FAIL;
            }
            if (bytes) buffer->Unlock();
            if (buffer) buffer->Release();
            sample->Release();
        }
        if (error == PublisherSourceError::None && FAILED(hr)) {
            error = PublisherSourceError::ReadFailure;
        }
        if (stopRequested()) error = PublisherSourceError::Stopped;
        releaseReader(reader);
        (void)selectedActivation->ShutdownObject();
        selectedActivation->Release();
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&encoder);
        finish(error);
    }
#endif

    void finish(PublisherSourceError error)
    {
        const std::lock_guard lock(mutex_);
        snapshot_.running = false;
        snapshot_.completed = error == PublisherSourceError::None;
        snapshot_.error = stopRequested_ ? PublisherSourceError::Stopped
                                         : error;
        changed_.notify_all();
    }

    mutable std::mutex mutex_;
    std::mutex joinMutex_;
    std::condition_variable changed_;
    std::thread worker_;
    PublisherSourceSnapshot snapshot_;
    bool started_ = false;
    bool stopRequested_ = false;
    camera_detail::CameraWorkerTestRun testRun_;
    camera_detail::CameraWorkerTestInterrupt testInterrupt_;
#ifdef _WIN32
    IMFSourceReader *readerForStop_ = nullptr;
#endif
};

CameraH264PublisherSource::CameraH264PublisherSource()
    : impl_(std::make_unique<Impl>()) {}
CameraH264PublisherSource::CameraH264PublisherSource(
    std::unique_ptr<Impl> impl
) : impl_(std::move(impl)) {}
CameraH264PublisherSource::~CameraH264PublisherSource() = default;

std::unique_ptr<CameraH264PublisherSource>
camera_detail::CameraSourceTestAccess::create(
    CameraWorkerTestRun run,
    CameraWorkerTestInterrupt interrupt
)
{
    return std::unique_ptr<CameraH264PublisherSource>(
        new CameraH264PublisherSource(std::make_unique<
            CameraH264PublisherSource::Impl
        >(std::move(run), std::move(interrupt)))
    );
}

PublisherSourceError CameraH264PublisherSource::listCameras(
    std::vector<CameraDeviceInfo> *devices
) noexcept
{
#ifdef _WIN32
    return enumerate(devices);
#else
    if (devices) devices->clear();
    return PublisherSourceError::PlatformUnsupported;
#endif
}

PublisherSourceError CameraH264PublisherSource::start(
    std::uint32_t cameraIndex,
    PublisherSubmitCallback submit
)
{
    return impl_->start(cameraIndex, std::move(submit));
}
PublisherSourceError CameraH264PublisherSource::waitForCompletion(
    std::chrono::milliseconds timeout
) { return impl_->waitForCompletion(timeout); }
PublisherSourceSnapshot CameraH264PublisherSource::snapshot() const noexcept
{ return impl_->snapshot(); }
void CameraH264PublisherSource::stop() noexcept { impl_->stop(); }
const char *CameraH264PublisherSource::errorName(
    PublisherSourceError error
) noexcept { return Mp4H264PublisherSource::errorName(error); }

} // namespace rtmp_monitor::publisher
