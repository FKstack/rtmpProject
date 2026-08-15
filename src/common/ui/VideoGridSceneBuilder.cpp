#include "ui/VideoGridSceneBuilder.h"

#include <utility>

RenderSnapshot VideoGridSceneBuilder::buildGrid(
    QSize logicalSize,
    qreal devicePixelRatio,
    const QVector<VideoGridSceneItem> &items,
    bool monitoringWallMode
)
{
    RenderSnapshot snapshot;
    snapshot.logicalCanvasSize = logicalSize;
    snapshot.devicePixelRatio = devicePixelRatio;
    snapshot.items.reserve(items.size());
    for (const VideoGridSceneItem &source : items) {
        if (source.streamId == kInvalidStreamId) {
            continue;
        }
        RenderItem item;
        item.streamId = source.streamId;
        item.tileRect = source.tileRect;
        item.videoViewport = source.videoViewport;
        item.displayMode = monitoringWallMode
                               ? VideoDisplayMode::Contain
                               : source.displayMode;
        item.title = source.title;
        item.status = source.status;
        item.frameVisible = source.frameVisible;
        snapshot.items.push_back(std::move(item));
    }
    return snapshot;
}

RenderSnapshot VideoGridSceneBuilder::buildFullscreen(
    QSize logicalSize,
    qreal devicePixelRatio,
    const VideoGridSceneItem &source
)
{
    RenderSnapshot snapshot;
    snapshot.logicalCanvasSize = logicalSize;
    snapshot.devicePixelRatio = devicePixelRatio;
    if (source.streamId != kInvalidStreamId) {
        RenderItem item;
        item.streamId = source.streamId;
        item.tileRect = QRect(QPoint(), logicalSize);
        item.videoViewport = item.tileRect;
        item.displayMode = VideoDisplayMode::Contain;
        item.title = source.title;
        item.status = source.status;
        item.frameVisible = source.frameVisible;
        item.fullscreen = true;
        snapshot.items.push_back(std::move(item));
    }
    return snapshot;
}
