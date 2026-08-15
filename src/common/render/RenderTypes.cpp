#include "render/RenderTypes.h"

#include <algorithm>

void RenderDirtyState::mark(RenderDirtyFlag flag) noexcept
{
    const RenderDirtyFlags bit = renderDirtyBit(flag);
    const RenderDirtyFlags previous = flags_.fetch_or(bit, std::memory_order_acq_rel);
    if ((previous & bit) != 0U) {
        mergeCount_.fetch_add(1, std::memory_order_relaxed);
    }
}

RenderDirtyFlags RenderDirtyState::pending() const noexcept
{
    return flags_.load(std::memory_order_acquire);
}

RenderDirtyFlags RenderDirtyState::consume() noexcept
{
    return flags_.exchange(0, std::memory_order_acq_rel);
}

std::uint64_t RenderDirtyState::mergeCount() const noexcept
{
    return mergeCount_.load(std::memory_order_relaxed);
}

VideoPlacement calculateVideoPlacement(
    const QRectF &viewport,
    const QSize &sourceSize,
    VideoDisplayMode mode
) noexcept
{
    VideoPlacement placement;
    if (viewport.isEmpty() || !sourceSize.isValid()) {
        return placement;
    }

    const qreal sourceAspect = static_cast<qreal>(sourceSize.width()) /
                               static_cast<qreal>(sourceSize.height());
    const qreal viewportAspect = viewport.width() / viewport.height();
    if (mode == VideoDisplayMode::Contain) {
        QSizeF target = sourceSize;
        target.scale(viewport.size(), Qt::KeepAspectRatio);
        placement.targetRect = QRectF(
            viewport.center().x() - target.width() / 2.0,
            viewport.center().y() - target.height() / 2.0,
            target.width(),
            target.height()
        );
        return placement;
    }

    placement.targetRect = viewport;
    if (sourceAspect > viewportAspect) {
        const qreal visibleWidth = viewportAspect / sourceAspect;
        placement.sourceUv = QRectF(
            (1.0 - visibleWidth) / 2.0, 0.0, visibleWidth, 1.0
        );
    } else if (sourceAspect < viewportAspect) {
        const qreal visibleHeight = sourceAspect / viewportAspect;
        placement.sourceUv = QRectF(
            0.0, (1.0 - visibleHeight) / 2.0, 1.0, visibleHeight
        );
    }
    return placement;
}

YuvColorTransform yuvColorTransform(
    const VideoColorDescription &description,
    int width,
    int height
) noexcept
{
    const VideoColorDescription color = resolvedVideoColorDescription(
        description, width, height
    );
    float redCr = 1.4020F;
    float greenCb = -0.344136F;
    float greenCr = -0.714136F;
    float blueCb = 1.7720F;
    if (color.matrix == VideoMatrixCoefficients::Bt709) {
        redCr = 1.5748F;
        greenCb = -0.187324F;
        greenCr = -0.468124F;
        blueCb = 1.8556F;
    } else if (color.matrix == VideoMatrixCoefficients::Bt2020Ncl) {
        redCr = 1.4746F;
        greenCb = -0.164553F;
        greenCr = -0.571353F;
        blueCb = 1.8814F;
    }

    const bool limited = color.range != VideoColorRange::Full;
    const float yScale = limited ? 255.0F / 219.0F : 1.0F;
    const float cScale = limited ? 255.0F / 224.0F : 1.0F;

    YuvColorTransform transform;
    transform.matrix.fill(0.0F);
    transform.matrix(0, 0) = yScale;
    transform.matrix(0, 2) = redCr * cScale;
    transform.matrix(1, 0) = yScale;
    transform.matrix(1, 1) = greenCb * cScale;
    transform.matrix(1, 2) = greenCr * cScale;
    transform.matrix(2, 0) = yScale;
    transform.matrix(2, 1) = blueCb * cScale;
    transform.offset = limited
                           ? QVector3D(-16.0F / 255.0F,
                                       -128.0F / 255.0F,
                                       -128.0F / 255.0F)
                           : QVector3D(0.0F, -0.5F, -0.5F);
    return transform;
}
