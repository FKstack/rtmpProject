#include "ui/VideoGridWidget.h"

#include <algorithm>
#include <utility>

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QParallelAnimationGroup>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSize>
#include <QShowEvent>
#include <QTimer>

#include "ui/VideoWidget.h"

namespace {

constexpr int kLayoutAnimationDurationMs = 220;
constexpr qreal kNewWidgetInitialScale = 0.85;
constexpr int kWindowGridBaseMargin = 4;
constexpr int kWindowGridSpacing = 4;
constexpr int kWallGridBaseMargin = 0;
constexpr int kWallGridSpacing = 0;

QSize largestAspectFit(QSize maximumSize, QSize aspect)
{
    if (maximumSize.width() <= 0 || maximumSize.height() <= 0 ||
        aspect.width() <= 0 || aspect.height() <= 0) {
        return {};
    }

    QSize best;
    const auto consider = [&best, maximumSize](QSize candidate) {
        if (candidate.width() <= 0 || candidate.height() <= 0 ||
            candidate.width() > maximumSize.width() ||
            candidate.height() > maximumSize.height()) {
            return;
        }
        if (candidate.width() * candidate.height() >
            best.width() * best.height()) {
            best = candidate;
        }
    };

    const qreal ratio = qreal(aspect.width()) / qreal(aspect.height());
    consider({maximumSize.width(),
              qRound(qreal(maximumSize.width()) / ratio)});
    consider({qRound(qreal(maximumSize.height()) * ratio),
              maximumSize.height()});
    return best;
}

struct CapturedWidget
{
    QPointer<VideoWidget> widget;
    QRect geometry;
    QPixmap pixmap;
};

struct WidgetAnimationItem
{
    QPointer<VideoWidget> widget;
    QRect startGeometry;
    QRect endGeometry;
    QPointer<QLabel> snapshot;
};

/**
 * @brief 以矩形中心为基准生成缩放后的矩形。
 */
QRect centeredScaledRect(const QRect &source, qreal scale)
{
    const QSize scaledSize(qRound(source.width() * scale),
                           qRound(source.height() * scale));
    QRect scaledRect(QPoint(), scaledSize);
    scaledRect.moveCenter(source.center());
    return scaledRect;
}

} // namespace

VideoGridWidget::VideoGridWidget(
    RendererPreference rendererPreference,
    QWidget *parent
)
    : QWidget(parent)
{
    canvasHost_ = new VideoCanvasHost(rendererPreference, this);
    canvasHost_->setObjectName(QStringLiteral("videoGridCanvas"));
    canvasHost_->setGeometry(rect());
    canvasHost_->lower();
    connect(canvasHost_, &VideoCanvasHost::surfacePresented,
            this, &VideoGridWidget::surfacePresented);

    gridLayout_ = new QGridLayout(this);
    gridLayout_->setContentsMargins(
        kWindowGridBaseMargin, kWindowGridBaseMargin,
        kWindowGridBaseMargin, kWindowGridBaseMargin
    );
    gridLayout_->setSpacing(kWindowGridSpacing);

    relayoutVideoWidgets();
}

void VideoGridWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    canvasHost_->setGeometry(rect());
    updateMonitoringGridGeometry();
    gridLayout_->activate();
    QTimer::singleShot(0, this, &VideoGridWidget::refreshRenderSnapshot);
}

void VideoGridWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event != nullptr &&
        (event->type() == QEvent::FontChange ||
         event->type() == QEvent::StyleChange)) {
        updateGeometry();
        relayoutVideoWidgets();
    }
}

void VideoGridWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    canvasHost_->lower();
    QTimer::singleShot(0, this, &VideoGridWidget::refreshRenderSnapshot);
}

GridDimensions VideoGridWidget::calculateGridDimensions(int widgetCount) noexcept
{
    if (widgetCount <= 0 || widgetCount > kMaximumVideoWidgetCount) {
        return {};
    }

    int columns = 1;
    while (columns < kMaximumGridDimension && columns * columns < widgetCount) {
        ++columns;
    }

    return {(widgetCount + columns - 1) / columns, columns};
}

MonitoringGridGeometry VideoGridWidget::calculateMonitoringGridGeometry(
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

    const int maximumVideoWidth =
        usableWidth / dimensions.columns - videoChromeSize.width();
    const int maximumVideoHeight =
        usableHeight / dimensions.rows - videoChromeSize.height();
    geometry.videoViewportSize = largestAspectFit(
        {maximumVideoWidth, maximumVideoHeight}, videoAspect
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
        0,
        availableSize.width() - baseMargins.left() - baseMargins.right() -
            gridSize.width()
    );
    const int verticalExtra = std::max(
        0,
        availableSize.height() - baseMargins.top() - baseMargins.bottom() -
            gridSize.height()
    );
    const int left = baseMargins.left() + horizontalExtra / 2;
    const int top = baseMargins.top() + verticalExtra / 2;
    geometry.layoutMargins = {
        left,
        top,
        baseMargins.right() + horizontalExtra - horizontalExtra / 2,
        baseMargins.bottom() + verticalExtra - verticalExtra / 2,
    };
    geometry.gridRect = QRect(QPoint(left, top), gridSize);
    return geometry;
}

GridDimensions VideoGridWidget::gridDimensions() const noexcept
{
    return calculateGridDimensions(videoWidgetCount());
}

int VideoGridWidget::videoWidgetCount() const noexcept
{
    return videoWidgets_.size();
}

VideoWidget *VideoGridWidget::videoWidgetAt(int index) const noexcept
{
    if (index < 0 || index >= videoWidgets_.size()) {
        return nullptr;
    }

    return videoWidgets_.at(index);
}

bool VideoGridWidget::canAddVideoWidget() const noexcept
{
    return interactionState_ == GridInteractionState::Idle &&
           videoWidgets_.size() < kMaximumVideoWidgetCount;
}

VideoGridWidget::GridInteractionState VideoGridWidget::interactionState() const noexcept
{
    return interactionState_;
}

MonitoringGridGeometry VideoGridWidget::monitoringGridGeometry() const noexcept
{
    return monitoringGridGeometry_;
}

bool VideoGridWidget::isMonitoringWallMode() const noexcept
{
    return monitoringWallMode_;
}

void VideoGridWidget::setMonitoringWallMode(bool enabled)
{
    if (monitoringWallMode_ == enabled) {
        return;
    }
    monitoringWallMode_ = enabled;
    gridLayout_->setSpacing(
        monitoringWallMode_ ? kWallGridSpacing : kWindowGridSpacing
    );
    updateMonitoringGridGeometry();
    updateGeometry();
    gridLayout_->invalidate();
    gridLayout_->activate();
    refreshRenderSnapshot();
    QTimer::singleShot(0, this, &VideoGridWidget::refreshRenderSnapshot);
}

QSize VideoGridWidget::minimumSizeHint() const
{
    constexpr int minimumVideoWidth = 144;
    constexpr int minimumVideoHeight = 81;
    const GridDimensions dimensions = gridDimensions();
    if (dimensions.rows <= 0 || dimensions.columns <= 0) {
        return QWidget::minimumSizeHint();
    }
    const QSize cellSize = maximumVideoChromeSizeHint() +
                           QSize(minimumVideoWidth, minimumVideoHeight);
    return {
        kWindowGridBaseMargin * 2 + dimensions.columns * cellSize.width() +
            (dimensions.columns - 1) * kWindowGridSpacing,
        kWindowGridBaseMargin * 2 + dimensions.rows * cellSize.height() +
            (dimensions.rows - 1) * kWindowGridSpacing,
    };
}

VideoWidget *VideoGridWidget::addVideoWidget()
{
    return addVideoWidget({});
}

VideoWidget *VideoGridWidget::addVideoWidget(const QString &deviceName)
{
    if (!canAddVideoWidget()) {
        if (videoWidgets_.size() >= kMaximumVideoWidgetCount) {
            emit maximumVideoWidgetCountReached();
        }
        return nullptr;
    }

    setInteractionState(GridInteractionState::AddingWidget);

    QVector<CapturedWidget> capturedWidgets;
    capturedWidgets.reserve(videoWidgets_.size());
    bool canAnimate = isVisible();
    for (auto *videoWidget : std::as_const(videoWidgets_)) {
        const QRect oldGeometry = videoWidget->geometry();
        if (oldGeometry.isEmpty()) {
            canAnimate = false;
        }
        capturedWidgets.append({
            videoWidget, oldGeometry, captureWidgetSnapshot(videoWidget)
        });
    }

    auto *newVideoWidget = createVideoWidget(deviceName);
    videoWidgets_.append(newVideoWidget);
    emit videoWidgetCountChanged(videoWidgets_.size());
    if (videoWidgets_.size() == kMaximumVideoWidgetCount) {
        emit maximumVideoWidgetCountReached();
    }

    relayoutVideoWidgets();
    newVideoWidget->show();
    gridLayout_->activate();

    const QRect newWidgetTargetGeometry = newVideoWidget->geometry();
    canAnimate = canAnimate && !newWidgetTargetGeometry.isEmpty();
    for (const CapturedWidget &capturedWidget : std::as_const(capturedWidgets)) {
        if (capturedWidget.widget == nullptr || capturedWidget.widget->geometry().isEmpty() ||
            capturedWidget.pixmap.isNull()) {
            canAnimate = false;
            break;
        }
    }

    if (!canAnimate) {
        for (auto *videoWidget : std::as_const(videoWidgets_)) {
            videoWidget->show();
        }
        setInteractionState(GridInteractionState::Idle);
        emit videoWidgetAdded(newVideoWidget);
        return newVideoWidget;
    }

    QVector<WidgetAnimationItem> animationItems;
    animationItems.reserve(videoWidgets_.size());
    for (const CapturedWidget &capturedWidget : std::as_const(capturedWidgets)) {
        auto *snapshot = createSnapshotOverlay(capturedWidget.pixmap,
                                               capturedWidget.geometry);
        animationItems.append({capturedWidget.widget,
                               capturedWidget.geometry,
                               capturedWidget.widget->geometry(),
                               snapshot});
    }

    const QPixmap newWidgetPixmap = captureWidgetSnapshot(newVideoWidget);
    const QRect newWidgetStartGeometry =
        centeredScaledRect(newWidgetTargetGeometry, kNewWidgetInitialScale);
    auto *newWidgetSnapshot = createSnapshotOverlay(newWidgetPixmap,
                                                    newWidgetStartGeometry);
    animationItems.append({newVideoWidget,
                           newWidgetStartGeometry,
                           newWidgetTargetGeometry,
                           newWidgetSnapshot});

    for (auto *videoWidget : std::as_const(videoWidgets_)) {
        videoWidget->hide();
    }

    auto *animationGroup = new QParallelAnimationGroup(this);
    interactionAnimation_ = animationGroup;

    for (const WidgetAnimationItem &item : std::as_const(animationItems)) {
        auto *geometryAnimation =
            new QPropertyAnimation(item.snapshot, "geometry", animationGroup);
        geometryAnimation->setDuration(kLayoutAnimationDurationMs);
        geometryAnimation->setStartValue(item.startGeometry);
        geometryAnimation->setEndValue(item.endGeometry);
        geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);
    }

    auto *opacityEffect = new QGraphicsOpacityEffect(newWidgetSnapshot);
    opacityEffect->setOpacity(0.0);
    newWidgetSnapshot->setGraphicsEffect(opacityEffect);
    auto *opacityAnimation =
        new QPropertyAnimation(opacityEffect, "opacity", animationGroup);
    opacityAnimation->setDuration(kLayoutAnimationDurationMs);
    opacityAnimation->setStartValue(0.0);
    opacityAnimation->setEndValue(1.0);
    opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);

    QPointer<VideoWidget> guardedNewWidget = newVideoWidget;
    connect(animationGroup, &QParallelAnimationGroup::finished, this,
            [this, animationGroup, animationItems, guardedNewWidget] {
                if (interactionAnimation_ != animationGroup) {
                    return;
                }

                for (const WidgetAnimationItem &item : animationItems) {
                    if (item.snapshot != nullptr) {
                        item.snapshot->deleteLater();
                    }
                    if (item.widget != nullptr) {
                        item.widget->show();
                    }
                }

                gridLayout_->activate();
                interactionAnimation_ = nullptr;
                setInteractionState(GridInteractionState::Idle);
                if (guardedNewWidget != nullptr) {
                    emit videoWidgetAdded(guardedNewWidget);
                }
            });

    animationGroup->start(QAbstractAnimation::DeleteWhenStopped);
    return newVideoWidget;
}

bool VideoGridWidget::removeVideoWidget(VideoWidget *videoWidget)
{
    if (interactionState_ != GridInteractionState::Idle ||
        videoWidget == nullptr || videoWidget == fullscreenVideoWidget_) {
        return false;
    }

    const int index = indexOf(videoWidget);
    if (index < 0) {
        return false;
    }

    videoWidgets_.removeAt(index);
    unbindVideoStream(videoWidget);
    gridLayout_->removeWidget(videoWidget);
    videoWidget->hide();
    emit videoWidgetRemoved(videoWidget);
    emit videoWidgetCountChanged(videoWidgets_.size());
    relayoutVideoWidgets();
    videoWidget->deleteLater();
    return true;
}

bool VideoGridWidget::isSwapAnimationInProgress() const noexcept
{
    return interactionState_ == GridInteractionState::SwappingWidgets;
}

bool VideoGridWidget::swapVideoWidgets(int firstIndex, int secondIndex)
{
    if (interactionState_ != GridInteractionState::Idle || firstIndex < 0 ||
        secondIndex < 0 || firstIndex >= videoWidgets_.size() ||
        secondIndex >= videoWidgets_.size() || firstIndex == secondIndex) {
        return false;
    }

    auto *firstWidget = videoWidgets_.at(firstIndex);
    auto *secondWidget = videoWidgets_.at(secondIndex);
    const QRect firstGeometry = firstWidget->geometry();
    const QRect secondGeometry = secondWidget->geometry();
    if (firstGeometry.isEmpty() || secondGeometry.isEmpty()) {
        return false;
    }

    auto *firstOverlay = createSnapshotOverlay(
        captureWidgetSnapshot(firstWidget), firstGeometry
    );
    auto *secondOverlay = createSnapshotOverlay(
        captureWidgetSnapshot(secondWidget), secondGeometry
    );

    setInteractionState(GridInteractionState::SwappingWidgets);
    firstWidget->hide();
    secondWidget->hide();

    std::swap(videoWidgets_[firstIndex], videoWidgets_[secondIndex]);
    relayoutVideoWidgets();

    auto *animationGroup = new QParallelAnimationGroup(this);
    interactionAnimation_ = animationGroup;

    auto *firstAnimation = new QPropertyAnimation(firstOverlay, "geometry", animationGroup);
    firstAnimation->setDuration(kLayoutAnimationDurationMs);
    firstAnimation->setStartValue(firstGeometry);
    firstAnimation->setEndValue(secondGeometry);
    firstAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto *secondAnimation = new QPropertyAnimation(secondOverlay, "geometry", animationGroup);
    secondAnimation->setDuration(kLayoutAnimationDurationMs);
    secondAnimation->setStartValue(secondGeometry);
    secondAnimation->setEndValue(firstGeometry);
    secondAnimation->setEasingCurve(QEasingCurve::OutCubic);

    QPointer<VideoWidget> guardedFirstWidget = firstWidget;
    QPointer<VideoWidget> guardedSecondWidget = secondWidget;
    QPointer<QLabel> guardedFirstOverlay = firstOverlay;
    QPointer<QLabel> guardedSecondOverlay = secondOverlay;
    connect(animationGroup, &QParallelAnimationGroup::finished, this,
            [this, animationGroup, guardedFirstWidget, guardedSecondWidget,
             guardedFirstOverlay, guardedSecondOverlay, firstIndex, secondIndex] {
                if (interactionAnimation_ != animationGroup) {
                    return;
                }

                if (guardedFirstOverlay != nullptr) {
                    guardedFirstOverlay->deleteLater();
                }
                if (guardedSecondOverlay != nullptr) {
                    guardedSecondOverlay->deleteLater();
                }
                if (guardedFirstWidget != nullptr) {
                    guardedFirstWidget->show();
                }
                if (guardedSecondWidget != nullptr) {
                    guardedSecondWidget->show();
                }

                gridLayout_->activate();
                interactionAnimation_ = nullptr;
                setInteractionState(GridInteractionState::Idle);
                emit videoWidgetsSwapped(firstIndex, secondIndex);
            });

    animationGroup->start(QAbstractAnimation::DeleteWhenStopped);
    return true;
}

void VideoGridWidget::notifyFullscreenEntryResult(VideoWidget *videoWidget, bool entered)
{
    if (interactionState_ != GridInteractionState::EnteringFullscreen ||
        fullscreenVideoWidget_ != videoWidget) {
        return;
    }

    if (entered) {
        setInteractionState(GridInteractionState::Fullscreen);
        return;
    }

    fullscreenVideoWidget_ = nullptr;
    setInteractionState(GridInteractionState::Idle);
}

void VideoGridWidget::notifyFullscreenExitStarted(VideoWidget *videoWidget)
{
    if (interactionState_ == GridInteractionState::Fullscreen &&
        fullscreenVideoWidget_ == videoWidget) {
        setInteractionState(GridInteractionState::ExitingFullscreen);
    }
}

void VideoGridWidget::notifyFullscreenExited(VideoWidget *videoWidget)
{
    if (interactionState_ != GridInteractionState::Fullscreen &&
        interactionState_ != GridInteractionState::ExitingFullscreen) {
        return;
    }

    if (fullscreenVideoWidget_ != nullptr && videoWidget != nullptr &&
        fullscreenVideoWidget_ != videoWidget) {
        return;
    }

    fullscreenVideoWidget_ = nullptr;
    setInteractionState(GridInteractionState::Idle);
    refreshRenderSnapshot();
}

void VideoGridWidget::bindVideoStream(
    VideoWidget *videoWidget,
    StreamId streamId,
    std::shared_ptr<LatestFrameMailbox> mailbox
)
{
    if (videoWidget == nullptr || indexOf(videoWidget) < 0 ||
        streamId == kInvalidStreamId || mailbox == nullptr) {
        return;
    }
    videoWidget->bindRenderSource(streamId, mailbox);
    canvasHost_->registerStream(streamId, std::move(mailbox));
    refreshRenderSnapshot();
}

void VideoGridWidget::unbindVideoStream(VideoWidget *videoWidget)
{
    if (videoWidget == nullptr) {
        return;
    }
    const StreamId streamId = videoWidget->streamId();
    if (streamId != kInvalidStreamId) {
        canvasHost_->unregisterStream(streamId);
    }
    videoWidget->unbindRenderSource();
    if (inCanvasFullscreen_ && videoWidget == fullscreenVideoWidget_) {
        // 画布内全屏的流被解绑时直接退出全屏，恢复网格视图。
        exitInCanvasFullscreen();
        return;
    }
    refreshRenderSnapshot();
}

bool VideoGridWidget::enterInCanvasFullscreen(VideoWidget *videoWidget)
{
    if (interactionState_ != GridInteractionState::EnteringFullscreen ||
        fullscreenVideoWidget_ != videoWidget || videoWidget == nullptr ||
        videoWidget->streamId() == kInvalidStreamId ||
        videoWidget->frameMailbox() == nullptr) {
        return false;
    }

    // 视频格只承载状态/标题等覆盖层；全屏期间隐藏，避免覆盖单路画面。
    for (VideoWidget *widget : videoWidgets_) {
        if (widget != nullptr) {
            widget->hide();
        }
    }
    inCanvasFullscreen_ = true;
    applyInCanvasFullscreenSnapshot();
    canvasHost_->setTargetFps(30);
    setFocusPolicy(Qt::StrongFocus);
    setFocus(Qt::OtherFocusReason);
    return true;
}

void VideoGridWidget::exitInCanvasFullscreen()
{
    if (!inCanvasFullscreen_) {
        return;
    }
    inCanvasFullscreen_ = false;
    setFocusPolicy(Qt::NoFocus);
    canvasHost_->setTargetFps(15);
    for (VideoWidget *widget : videoWidgets_) {
        if (widget != nullptr) {
            widget->show();
        }
    }
    fullscreenVideoWidget_ = nullptr;
    setInteractionState(GridInteractionState::Idle);
    refreshRenderSnapshot();
}

bool VideoGridWidget::isInCanvasFullscreenActive() const noexcept
{
    return inCanvasFullscreen_;
}

void VideoGridWidget::applyInCanvasFullscreenSnapshot()
{
    RenderSnapshot snapshot;
    snapshot.logicalCanvasSize = size();
    snapshot.devicePixelRatio = devicePixelRatioF();
    VideoWidget *videoWidget = fullscreenVideoWidget_;
    if (videoWidget != nullptr && videoWidget->streamId() != kInvalidStreamId) {
        RenderItem item;
        item.streamId = videoWidget->streamId();
        item.tileRect = rect();
        item.videoViewport = rect();
        item.displayMode = VideoDisplayMode::Contain;
        item.title = videoWidget->deviceName();
        item.status = videoWidget->statusText();
        item.frameVisible = videoWidget->isFrameVisible();
        item.fullscreen = true;
        snapshot.items.push_back(std::move(item));
    }
    canvasHost_->setSnapshot(std::move(snapshot));
}

void VideoGridWidget::keyPressEvent(QKeyEvent *event)
{
    if (inCanvasFullscreen_ && event != nullptr &&
        event->key() == Qt::Key_Escape) {
        exitInCanvasFullscreen();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void VideoGridWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (inCanvasFullscreen_) {
        exitInCanvasFullscreen();
        if (event != nullptr) {
            event->accept();
        }
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void VideoGridWidget::refreshRenderSnapshot()
{
    if (inCanvasFullscreen_) {
        applyInCanvasFullscreenSnapshot();
        return;
    }
    RenderSnapshot snapshot;
    snapshot.logicalCanvasSize = size();
    snapshot.devicePixelRatio = devicePixelRatioF();
    snapshot.items.reserve(videoWidgets_.size());
    for (int index = 0; index < videoWidgets_.size(); ++index) {
        VideoWidget *videoWidget = videoWidgets_.at(index);
        if (videoWidget == nullptr ||
            videoWidget->streamId() == kInvalidStreamId) {
            continue;
        }
        RenderItem item;
        item.streamId = videoWidget->streamId();
        item.tileRect = videoWidget->geometry();
        item.videoViewport = videoWidget->videoViewportRect(this);
        item.displayMode = monitoringWallMode_
                               ? VideoDisplayMode::Contain
                               : videoWidget->displayMode();
        item.title = videoWidget->deviceName();
        item.status = videoWidget->statusText();
        // frameVisible is the semantic stream state. Transient QWidget
        // visibility during stacked-page changes or preload animations must not
        // be frozen into the renderer snapshot; the canvas scheduler already
        // pauses when the canvas itself is hidden.
        item.frameVisible = videoWidget->isFrameVisible();
        snapshot.items.push_back(std::move(item));
    }
    canvasHost_->setSnapshot(std::move(snapshot));
}

QString VideoGridWidget::activeRendererBackend() const
{
    return canvasHost_->activeBackendName();
}

RenderStatistics VideoGridWidget::renderStatistics() const noexcept
{
    return canvasHost_->statistics();
}

RenderRuntimeMetrics VideoGridWidget::rendererRuntimeMetrics() const
{
    return canvasHost_->runtimeMetrics();
}

VideoWidget *VideoGridWidget::createVideoWidget(const QString &deviceName)
{
    auto *videoWidget = new VideoWidget(this);
    const int cameraNumber = nextCameraNumber_++;
    videoWidget->setObjectName(
        QStringLiteral("videoWidget%1").arg(cameraNumber, 2, 10, QLatin1Char('0'))
    );
    videoWidget->setDeviceName(
        deviceName.trimmed().isEmpty()
            ? QStringLiteral("Camera %1")
                  .arg(cameraNumber, 2, 10, QLatin1Char('0'))
            : deviceName.trimmed()
    );
    videoWidget->setStatusText(tr("未连接"));
    videoWidget->setDragEnabled(interactionState_ == GridInteractionState::Idle);
    connectVideoWidgetSignals(videoWidget);
    return videoWidget;
}

void VideoGridWidget::connectVideoWidgetSignals(VideoWidget *videoWidget)
{
    connect(videoWidget, &VideoWidget::swapRequested,
            this, &VideoGridWidget::handleSwapRequested, Qt::UniqueConnection);
    connect(videoWidget, &VideoWidget::fullscreenRequested,
            this, &VideoGridWidget::handleFullscreenRequested, Qt::UniqueConnection);
    connect(videoWidget, &VideoWidget::renderStateChanged,
            this, &VideoGridWidget::handleRenderStateChanged,
            Qt::UniqueConnection);
}

void VideoGridWidget::handleRenderStateChanged(VideoWidget *videoWidget)
{
    if (videoWidget == nullptr || indexOf(videoWidget) < 0) {
        return;
    }
    refreshRenderSnapshot();
}

void VideoGridWidget::handleSwapRequested(VideoWidget *source, VideoWidget *target)
{
    const int sourceIndex = indexOf(source);
    const int targetIndex = indexOf(target);
    if (sourceIndex >= 0 && targetIndex >= 0) {
        swapVideoWidgets(sourceIndex, targetIndex);
    }
}

void VideoGridWidget::handleFullscreenRequested(VideoWidget *videoWidget)
{
    if (interactionState_ != GridInteractionState::Idle || indexOf(videoWidget) < 0) {
        return;
    }

    fullscreenVideoWidget_ = videoWidget;
    setInteractionState(GridInteractionState::EnteringFullscreen);
    emit fullscreenRequested(videoWidget);
}

int VideoGridWidget::indexOf(const VideoWidget *videoWidget) const noexcept
{
    return videoWidgets_.indexOf(const_cast<VideoWidget *>(videoWidget));
}

void VideoGridWidget::relayoutVideoWidgets()
{
    for (auto *videoWidget : std::as_const(videoWidgets_)) {
        gridLayout_->removeWidget(videoWidget);
    }

    for (int index = 0; index < kMaximumGridDimension; ++index) {
        gridLayout_->setRowStretch(index, 0);
        gridLayout_->setColumnStretch(index, 0);
    }

    const GridDimensions dimensions = gridDimensions();
    for (int index = 0; index < videoWidgets_.size(); ++index) {
        gridLayout_->addWidget(videoWidgets_.at(index),
                               index / dimensions.columns,
                               index % dimensions.columns);
    }

    for (int row = 0; row < dimensions.rows; ++row) {
        gridLayout_->setRowStretch(row, 1);
    }
    for (int column = 0; column < dimensions.columns; ++column) {
        gridLayout_->setColumnStretch(column, 1);
    }

    updateMonitoringGridGeometry();
    updateGeometry();
    gridLayout_->invalidate();
    gridLayout_->activate();
    QTimer::singleShot(0, this, &VideoGridWidget::refreshRenderSnapshot);
}

void VideoGridWidget::updateMonitoringGridGeometry()
{
    const int baseMargin = monitoringWallMode_
                               ? kWallGridBaseMargin
                               : kWindowGridBaseMargin;
    const int spacing = monitoringWallMode_
                            ? kWallGridSpacing
                            : kWindowGridSpacing;
    monitoringGridGeometry_ = calculateMonitoringGridGeometry(
        size(), gridDimensions(), maximumVideoChromeSizeHint(),
        QMargins(baseMargin, baseMargin, baseMargin, baseMargin), spacing
    );
    if (monitoringGridGeometry_.isValid()) {
        gridLayout_->setContentsMargins(monitoringGridGeometry_.layoutMargins);
    } else {
        gridLayout_->setContentsMargins(
            baseMargin, baseMargin, baseMargin, baseMargin
        );
    }
    gridLayout_->invalidate();
}

QSize VideoGridWidget::maximumVideoChromeSizeHint() const
{
    QSize maximum;
    for (const VideoWidget *videoWidget : videoWidgets_) {
        if (videoWidget == nullptr) {
            continue;
        }
        const QSize chrome = videoWidget->videoChromeSizeHint();
        maximum.setWidth(std::max(maximum.width(), chrome.width()));
        maximum.setHeight(std::max(maximum.height(), chrome.height()));
    }
    return maximum;
}

void VideoGridWidget::setInteractionState(GridInteractionState state)
{
    if (interactionState_ == state) {
        return;
    }

    interactionState_ = state;
    setDragEnabledForAll(interactionState_ == GridInteractionState::Idle);
    emit gridInteractionStateChanged(interactionState_);
}

void VideoGridWidget::setDragEnabledForAll(bool enabled)
{
    for (auto *videoWidget : std::as_const(videoWidgets_)) {
        videoWidget->setDragEnabled(enabled);
    }
}

QLabel *VideoGridWidget::createSnapshotOverlay(const QPixmap &pixmap,
                                               const QRect &geometry)
{
    auto *overlay = new QLabel(this);
    overlay->setPixmap(pixmap);
    overlay->setScaledContents(true);
    overlay->setGeometry(geometry);
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->show();
    overlay->raise();
    return overlay;
}

QPixmap VideoGridWidget::captureWidgetSnapshot(VideoWidget *videoWidget)
{
    if (videoWidget == nullptr) {
        return {};
    }
    QPixmap snapshot = videoWidget->grab();
    if (!videoWidget->isFrameVisible() || snapshot.isNull()) {
        return snapshot;
    }
    QImage canvas = canvasHost_->grabFramebufferImage();
    if (canvas.isNull()) {
        return snapshot;
    }
    const qreal dpr = std::max<qreal>(1.0, canvas.devicePixelRatio());
    const QRect viewport = videoWidget->videoViewportRect(this);
    const QRect source(
        qRound(viewport.x() * dpr),
        qRound(viewport.y() * dpr),
        qRound(viewport.width() * dpr),
        qRound(viewport.height() * dpr)
    );
    if (source.isEmpty()) {
        return snapshot;
    }
    QImage videoImage = canvas.copy(source.intersected(canvas.rect()));
    videoImage.setDevicePixelRatio(dpr);
    const QPoint destinationTopLeft = videoWidget->mapFrom(
        this, viewport.topLeft()
    );
    QPainter painter(&snapshot);
    painter.drawImage(QRect(destinationTopLeft, viewport.size()), videoImage);
    return snapshot;
}
