#include "ui/VideoGridWidget.h"

#include <cstddef>
#include <utility>

#include <QAbstractAnimation>
#include <QGridLayout>
#include <QLatin1Char>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

#include "ui/VideoWidget.h"

namespace {

constexpr int kSwapAnimationDurationMs = 220;

} // namespace

VideoGridWidget::VideoGridWidget(QWidget *parent)
    : QWidget(parent)
{
    // 第二周固定为 2x2，先验证布局和单路槽位边界，动态宫格留到设备管理模块完成后处理。
    gridLayout_ = new QGridLayout(this);
    gridLayout_->setContentsMargins(12, 12, 12, 12);
    gridLayout_->setSpacing(12);

    for (int index = 0; index < kVideoWidgetCount; ++index) {
        auto *videoWidget = new VideoWidget(this);
        videoWidget->setObjectName(QStringLiteral("videoWidget%1").arg(index + 1));
        // 设备名称从 1 开始展示，避免将内部 0 基索引暴露给用户界面。
        videoWidget->setDeviceName(
            QStringLiteral("camera%1").arg(index + 1, 3, 10, QLatin1Char('0'))
        );
        videoWidget->setStatusText(tr("未连接"));

        gridLayout_->addWidget(videoWidget, index / kColumnCount, index % kColumnCount);
        videoWidgets_[static_cast<std::size_t>(index)] = videoWidget;

        connect(videoWidget, &VideoWidget::swapRequested,
                this, &VideoGridWidget::handleSwapRequested);
        connect(videoWidget, &VideoWidget::fullscreenRequested, this,
                [this](VideoWidget *requestedVideoWidget) {
                    if (!swapAnimationInProgress_) {
                        emit fullscreenRequested(requestedVideoWidget);
                    }
                });
    }

    gridLayout_->setColumnStretch(0, 1);
    gridLayout_->setColumnStretch(1, 1);
    gridLayout_->setRowStretch(0, 1);
    gridLayout_->setRowStretch(1, 1);
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

bool VideoGridWidget::isSwapAnimationInProgress() const noexcept
{
    return swapAnimationInProgress_;
}

bool VideoGridWidget::swapVideoWidgets(int firstIndex, int secondIndex)
{
    if (swapAnimationInProgress_ || firstIndex < 0 || secondIndex < 0 ||
        firstIndex >= kVideoWidgetCount || secondIndex >= kVideoWidgetCount ||
        firstIndex == secondIndex) {
        return false;
    }

    auto *firstWidget = videoWidgets_[static_cast<std::size_t>(firstIndex)];
    auto *secondWidget = videoWidgets_[static_cast<std::size_t>(secondIndex)];
    const QRect firstGeometry = firstWidget->geometry();
    const QRect secondGeometry = secondWidget->geometry();
    if (firstGeometry.isEmpty() || secondGeometry.isEmpty()) {
        return false;
    }

    // 快照先于布局调整创建，确保动画展示的是交换前的完整控件内容和视觉状态。
    auto *firstOverlay = createSnapshotOverlay(firstWidget);
    auto *secondOverlay = createSnapshotOverlay(secondWidget);

    setDragEnabledForAll(false);
    firstWidget->hide();
    secondWidget->hide();

    gridLayout_->removeWidget(firstWidget);
    gridLayout_->removeWidget(secondWidget);
    std::swap(videoWidgets_[static_cast<std::size_t>(firstIndex)],
              videoWidgets_[static_cast<std::size_t>(secondIndex)]);
    gridLayout_->addWidget(secondWidget, firstIndex / kColumnCount, firstIndex % kColumnCount);
    gridLayout_->addWidget(firstWidget, secondIndex / kColumnCount, secondIndex % kColumnCount);
    gridLayout_->activate();

    swapAnimationInProgress_ = true;
    swapAnimation_ = new QParallelAnimationGroup(this);

    auto *firstAnimation = new QPropertyAnimation(firstOverlay, "geometry", swapAnimation_);
    firstAnimation->setDuration(kSwapAnimationDurationMs);
    firstAnimation->setStartValue(firstGeometry);
    firstAnimation->setEndValue(secondGeometry);
    firstAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto *secondAnimation = new QPropertyAnimation(secondOverlay, "geometry", swapAnimation_);
    secondAnimation->setDuration(kSwapAnimationDurationMs);
    secondAnimation->setStartValue(secondGeometry);
    secondAnimation->setEndValue(firstGeometry);
    secondAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto *animation = swapAnimation_;
    connect(animation, &QParallelAnimationGroup::finished, this,
            [this, animation, firstWidget, secondWidget, firstOverlay, secondOverlay,
             firstIndex, secondIndex] {
                if (swapAnimation_ != animation) {
                    return;
                }

                firstOverlay->deleteLater();
                secondOverlay->deleteLater();
                firstWidget->show();
                secondWidget->show();
                gridLayout_->activate();

                swapAnimationInProgress_ = false;
                swapAnimation_ = nullptr;
                setDragEnabledForAll(true);
                emit videoWidgetsSwapped(firstIndex, secondIndex);
            });

    animation->start(QAbstractAnimation::DeleteWhenStopped);
    return true;
}

void VideoGridWidget::handleSwapRequested(VideoWidget *source, VideoWidget *target)
{
    const int sourceIndex = indexOf(source);
    const int targetIndex = indexOf(target);
    if (sourceIndex >= 0 && targetIndex >= 0) {
        swapVideoWidgets(sourceIndex, targetIndex);
    }
}

int VideoGridWidget::indexOf(const VideoWidget *videoWidget) const noexcept
{
    for (int index = 0; index < kVideoWidgetCount; ++index) {
        if (videoWidgets_[static_cast<std::size_t>(index)] == videoWidget) {
            return index;
        }
    }

    return -1;
}

void VideoGridWidget::setDragEnabledForAll(bool enabled)
{
    for (auto *videoWidget : videoWidgets_) {
        videoWidget->setDragEnabled(enabled);
    }
}

QLabel *VideoGridWidget::createSnapshotOverlay(VideoWidget *videoWidget)
{
    auto *overlay = new QLabel(this);
    overlay->setPixmap(videoWidget->grab());
    overlay->setGeometry(videoWidget->geometry());
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->show();
    overlay->raise();
    return overlay;
}
