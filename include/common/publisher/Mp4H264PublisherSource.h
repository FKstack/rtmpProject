#pragma once

#include "h264/H264MediaContracts.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace rtmp_monitor::publisher {

enum class PublisherSourceError {
    None,
    InvalidState,
    FileNotFound,
    OpenFailed,
    VideoStreamMissing,
    H264Required,
    BFramesUnsupported,
    BitstreamFilterFailure,
    ReadFailure,
    SubmitFailure,
    Stopped,
    Timeout,
};

struct PublisherSourceSnapshot
{
    PublisherSourceError error = PublisherSourceError::None;
    bool running = false;
    bool completed = false;
    std::uint64_t emittedAccessUnits = 0;
    std::uint64_t emittedKeyframes = 0;
    std::uint64_t droppedAccessUnits = 0;
};

using PublisherSubmitCallback =
    std::function<H264SubmitResult(H264AccessUnit)>;

/** Owns local MP4 demux, H.264 Annex-B conversion and pacing lifecycle. */
class Mp4H264PublisherSource final
{
public:
    Mp4H264PublisherSource();
    ~Mp4H264PublisherSource();

    Mp4H264PublisherSource(const Mp4H264PublisherSource &) = delete;
    Mp4H264PublisherSource &operator=(const Mp4H264PublisherSource &) = delete;

    [[nodiscard]] PublisherSourceError start(
        std::string filePath,
        PublisherSubmitCallback submit
    );
    [[nodiscard]] PublisherSourceError waitForCompletion(
        std::chrono::milliseconds timeout
    );
    [[nodiscard]] PublisherSourceSnapshot snapshot() const noexcept;
    void stop() noexcept;

    [[nodiscard]] static const char *errorName(
        PublisherSourceError error
    ) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtmp_monitor::publisher
