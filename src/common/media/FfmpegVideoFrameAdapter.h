#pragma once

#include <optional>

#include "media/VideoFrame.h"

struct AVFrame;

struct FfmpegVideoFrameAdapter
{
    static std::optional<VideoFrame> adapt(
        const AVFrame *frame,
        VideoRational timeBase,
        std::uint64_t sequence,
        std::uint64_t sessionGeneration,
        qint64 receivedMonotonicMs,
        qint64 sourceTimestampMs = -1,
        std::optional<std::uint32_t> sourceSequence = std::nullopt
    );
};
