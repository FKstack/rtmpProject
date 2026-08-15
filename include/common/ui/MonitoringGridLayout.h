#pragma once

#include <QMargins>
#include <QRect>
#include <QSize>

struct GridDimensions
{
    int rows {};
    int columns {};
};

struct MonitoringGridGeometry
{
    QRect gridRect;
    QSize cellSize;
    QSize videoViewportSize;
    QMargins layoutMargins;

    [[nodiscard]] bool isValid() const noexcept
    {
        return !gridRect.isEmpty() && cellSize.isValid() &&
               videoViewportSize.isValid();
    }
};

/** @brief Pure monitoring-wall geometry calculations. */
class MonitoringGridLayout final
{
public:
    [[nodiscard]] static GridDimensions dimensionsForCount(
        int widgetCount, int maximumCount = 16,
        int maximumDimension = 4
    ) noexcept;

    [[nodiscard]] static MonitoringGridGeometry calculate(
        QSize availableSize,
        GridDimensions dimensions,
        QSize videoChromeSize,
        QMargins baseMargins = QMargins(4, 4, 4, 4),
        int spacing = 4,
        QSize videoAspect = QSize(16, 9)
    ) noexcept;
};
