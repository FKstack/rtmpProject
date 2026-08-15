#include "ui/MonitoringGridLayout.h"

#include <algorithm>

#include <QtGlobal>

namespace {

QSize largestAspectFit(QSize maximumSize, QSize aspect)
{
    if (maximumSize.width() <= 0 || maximumSize.height() <= 0 ||
        aspect.width() <= 0 || aspect.height() <= 0) {
        return {};
    }
    QSize best;
    const auto consider = [&best, maximumSize](QSize candidate) {
        if (candidate.width() > 0 && candidate.height() > 0 &&
            candidate.width() <= maximumSize.width() &&
            candidate.height() <= maximumSize.height() &&
            candidate.width() * candidate.height() >
                best.width() * best.height()) {
            best = candidate;
        }
    };
    const qreal ratio = qreal(aspect.width()) / qreal(aspect.height());
    consider({maximumSize.width(), qRound(maximumSize.width() / ratio)});
    consider({qRound(maximumSize.height() * ratio), maximumSize.height()});
    return best;
}

} // namespace

GridDimensions MonitoringGridLayout::dimensionsForCount(
    int widgetCount,
    int maximumCount,
    int maximumDimension
) noexcept
{
    if (widgetCount <= 0 || widgetCount > maximumCount || maximumDimension <= 0) {
        return {};
    }
    int columns = 1;
    while (columns < maximumDimension && columns * columns < widgetCount) {
        ++columns;
    }
    return {(widgetCount + columns - 1) / columns, columns};
}

MonitoringGridGeometry MonitoringGridLayout::calculate(
    QSize availableSize,
    GridDimensions dimensions,
    QSize videoChromeSize,
    QMargins baseMargins,
    int spacing,
    QSize videoAspect
) noexcept
{
    MonitoringGridGeometry geometry;
    if (availableSize.width() <= 0 || availableSize.height() <= 0 ||
        dimensions.rows <= 0 || dimensions.columns <= 0 || spacing < 0 ||
        videoAspect.width() <= 0 || videoAspect.height() <= 0 ||
        videoChromeSize.width() < 0 || videoChromeSize.height() < 0) {
        return geometry;
    }
    const int horizontalGaps = (dimensions.columns - 1) * spacing;
    const int verticalGaps = (dimensions.rows - 1) * spacing;
    const int usableWidth = availableSize.width() - baseMargins.left() -
                            baseMargins.right() - horizontalGaps;
    const int usableHeight = availableSize.height() - baseMargins.top() -
                             baseMargins.bottom() - verticalGaps;
    if (usableWidth <= 0 || usableHeight <= 0) {
        return geometry;
    }
    geometry.videoViewportSize = largestAspectFit(
        {usableWidth / dimensions.columns - videoChromeSize.width(),
         usableHeight / dimensions.rows - videoChromeSize.height()},
        videoAspect
    );
    if (!geometry.videoViewportSize.isValid()) {
        return geometry;
    }
    geometry.cellSize = geometry.videoViewportSize + videoChromeSize;
    const QSize gridSize(
        dimensions.columns * geometry.cellSize.width() + horizontalGaps,
        dimensions.rows * geometry.cellSize.height() + verticalGaps
    );
    const int horizontalExtra = std::max(
        0, availableSize.width() - baseMargins.left() - baseMargins.right() -
               gridSize.width()
    );
    const int verticalExtra = std::max(
        0, availableSize.height() - baseMargins.top() - baseMargins.bottom() -
               gridSize.height()
    );
    const int left = baseMargins.left() + horizontalExtra / 2;
    const int top = baseMargins.top() + verticalExtra / 2;
    geometry.layoutMargins = {
        left, top,
        baseMargins.right() + horizontalExtra - horizontalExtra / 2,
        baseMargins.bottom() + verticalExtra - verticalExtra / 2,
    };
    geometry.gridRect = QRect(QPoint(left, top), gridSize);
    return geometry;
}
