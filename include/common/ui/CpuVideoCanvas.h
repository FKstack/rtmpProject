#pragma once

#include <QWidget>

#include <unordered_map>

#include "media/PlaybackTypes.h"
#include "media/VideoFrameConverter.h"

class VideoCanvasHost;

/**
 * @brief QPainter-based CPU compositor; contains no EGL/GLES dependency.
 *
 * Converts each visible VideoFrame to a reusable QImage through
 * VideoFrameToImageConverter and paints all snapshot items in one pass.
 * All access to render state goes through the owning VideoCanvasHost.
 */
class CpuVideoCanvas final : public QWidget
{
public:
    explicit CpuVideoCanvas(VideoCanvasHost *host, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct FrameCache
    {
        std::uint64_t sequence = 0;
        QImage image;
        VideoFrameToImageConverter converter;
    };

    VideoCanvasHost *host_ = nullptr;
    std::unordered_map<StreamId, FrameCache> caches_;
};
