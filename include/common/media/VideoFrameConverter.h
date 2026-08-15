#pragma once

#include <QImage>

#include "media/VideoFrame.h"

/** @brief Diagnostic CPU conversion used by the QPainter fallback and tests. */
class VideoFrameToImageConverter final
{
public:
    VideoFrameToImageConverter() = default;
    ~VideoFrameToImageConverter();

    VideoFrameToImageConverter(const VideoFrameToImageConverter &) = delete;
    VideoFrameToImageConverter &operator=(const VideoFrameToImageConverter &) = delete;

    [[nodiscard]] QImage convert(const VideoFrame &frame);
    void reset() noexcept;

private:
    void *context_ = nullptr;
};
