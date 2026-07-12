#include "ui/VideoGridWidget.h"

#include <cstddef>

#include <QGridLayout>
#include <QLatin1Char>

#include "ui/VideoWidget.h"

VideoGridWidget::VideoGridWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *gridLayout = new QGridLayout(this);
    gridLayout->setContentsMargins(12, 12, 12, 12);
    gridLayout->setSpacing(12);

    for (int index = 0; index < kVideoWidgetCount; ++index) {
        auto *videoWidget = new VideoWidget(this);
        videoWidget->setObjectName(QStringLiteral("videoWidget%1").arg(index + 1));
        videoWidget->setDeviceName(
            QStringLiteral("camera%1").arg(index + 1, 3, 10, QLatin1Char('0'))
        );
        videoWidget->setStatusText(tr("未连接"));

        gridLayout->addWidget(videoWidget, index / 2, index % 2);
        videoWidgets_[static_cast<std::size_t>(index)] = videoWidget;
    }

    gridLayout->setColumnStretch(0, 1);
    gridLayout->setColumnStretch(1, 1);
    gridLayout->setRowStretch(0, 1);
    gridLayout->setRowStretch(1, 1);
}

int VideoGridWidget::videoWidgetCount() const noexcept
{
    return kVideoWidgetCount;
}

VideoWidget *VideoGridWidget::videoWidgetAt(int index) const noexcept
{
    if (index < 0 || index >= kVideoWidgetCount) {
        return nullptr;
    }

    return videoWidgets_[static_cast<std::size_t>(index)];
}
