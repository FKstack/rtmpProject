#pragma once

#include <QRect>
#include <QString>
#include <QVector>

#include "render/RenderTypes.h"

struct VideoGridSceneItem
{
    StreamId streamId = kInvalidStreamId;
    QRect tileRect;
    QRect videoViewport;
    VideoDisplayMode displayMode = VideoDisplayMode::Contain;
    QString title;
    QString status;
    bool frameVisible = false;
};

/** @brief Builds renderer snapshots from UI-neutral grid scene values. */
class VideoGridSceneBuilder final
{
public:
    [[nodiscard]] static RenderSnapshot buildGrid(
        QSize logicalSize, qreal devicePixelRatio,
        const QVector<VideoGridSceneItem> &items, bool monitoringWallMode
    );
    [[nodiscard]] static RenderSnapshot buildFullscreen(
        QSize logicalSize, qreal devicePixelRatio,
        const VideoGridSceneItem &item
    );
};
