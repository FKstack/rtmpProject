#include "ui/FullscreenVideoWindow.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#include "ui/FullscreenControlBar.h"
#include "ui/VideoWidget.h"

FullscreenVideoWindow::FullscreenVideoWindow(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("fullscreenVideoWindow"));
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // 顶层窗口必须独立于外部 QSS 保持纯黑，避免控件换 parent 时暴露桌面或系统底色。
    QPalette fullscreenPalette = palette();
    fullscreenPalette.setColor(QPalette::Window, Qt::black);
    setPalette(fullscreenPalette);

    videoLayout_ = new QVBoxLayout(this);
    videoLayout_->setContentsMargins(0, 0, 0, 0);
    videoLayout_->setSpacing(0);

    controlBar_ = new FullscreenControlBar(this);
    controlBar_->hide();

    autoHideTimer_ = new QTimer(this);
    autoHideTimer_->setSingleShot(true);

    connect(autoHideTimer_, &QTimer::timeout, this, &FullscreenVideoWindow::hideControlBar);
    connect(controlBar_, &FullscreenControlBar::exitRequested,
            this, &FullscreenVideoWindow::exitFullscreen);
    connect(controlBar_, &FullscreenControlBar::pointerEntered, this, [this] {
        autoHideTimer_->stop();
        unsetCursor();
    });
    connect(controlBar_, &FullscreenControlBar::pointerLeft,
            this, &FullscreenVideoWindow::scheduleControlBarHide);
    connect(controlBar_, &FullscreenControlBar::muteRequested, this, [this] {
        if (restoreState_.videoWidget != nullptr) {
            emit muteRequested(restoreState_.videoWidget);
        }
        showControlBar(true);
    });
    connect(controlBar_, &FullscreenControlBar::screenshotRequested, this, [this] {
        if (restoreState_.videoWidget != nullptr) {
            emit screenshotRequested(restoreState_.videoWidget);
        }
        showControlBar(true);
    });
}

FullscreenVideoWindow::~FullscreenVideoWindow()
{
    // MainWindow 结束时先恢复视频区域，避免视频区域因临时父对象变化而脱离原控件树。
    exitFullscreen();
}

bool FullscreenVideoWindow::enterFullscreen(VideoWidget *videoWidget)
{
    if (transitionState_ != TransitionState::Windowed || videoWidget == nullptr ||
        !videoWidget->isVisible()) {
        return false;
    }

    transitionState_ = TransitionState::Entering;

    auto *sourceLayout = qobject_cast<QVBoxLayout *>(videoWidget->layout());
    QFrame *videoSurface = videoWidget->videoSurfaceForFullscreen();
    if (sourceLayout == nullptr || videoSurface == nullptr) {
        transitionState_ = TransitionState::Windowed;
        return false;
    }

    const int layoutIndex = sourceLayout->indexOf(videoSurface);
    if (layoutIndex < 0) {
        transitionState_ = TransitionState::Windowed;
        return false;
    }

    QScreen *targetScreen = screenForVideoWidget(videoWidget);

    restoreState_.videoWidget = videoWidget;
    restoreState_.originalParent = videoSurface->parentWidget();
    restoreState_.originalLayout = sourceLayout;
    restoreState_.layoutIndex = layoutIndex;
    restoreState_.layoutStretch = sourceLayout->stretch(layoutIndex);
    restoreState_.sizePolicy = videoSurface->sizePolicy();
    restoreState_.surfaceWasVisible = videoSurface->isVisible();
    restoreState_.statusLabelWasVisible = videoWidget->isStatusLabelVisible();

    sourceLayout->removeWidget(videoSurface);
    videoWidget->setFullscreenSurfaceMode(true);
    videoSurface->setParent(this);
    videoSurface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLayout_->addWidget(videoSurface);
    activeVideoSurface_ = videoSurface;

    controlBar_->setDeviceName(videoWidget->deviceName());
    controlBar_->setStreamInfo(videoWidget->statusText());

    if (targetScreen != nullptr) {
        setScreen(targetScreen);
        setGeometry(targetScreen->geometry());
    }

    // setParent() 会使控件暂时隐藏；必须在顶层窗口出现前完成可见性和布局准备。
    videoSurface->show();
    videoLayout_->activate();
    controlBar_->show();
    positionControlBar();
    controlBar_->raise();

    showFullScreen();
    transitionState_ = TransitionState::Fullscreen;
    raise();
    activateWindow();
    setFocus();
    showControlBar(true);
    emit fullscreenEntered(videoWidget);
    return true;
}

void FullscreenVideoWindow::exitFullscreen()
{
    if (transitionState_ != TransitionState::Fullscreen || !isFullscreenActive()) {
        return;
    }

    transitionState_ = TransitionState::Exiting;

    auto *videoSurface = activeVideoSurface_;
    QPointer<VideoWidget> videoWidget = restoreState_.videoWidget;
    const VideoSurfaceRestoreState restoreState = restoreState_;

    // 动态网格先进入退出互斥状态，避免恢复 parent 和布局时启动新的布局动画。
    emit fullscreenExitStarted(videoWidget);

    autoHideTimer_->stop();
    controlBar_->hide();
    unsetCursor();

    videoSurface->hide();
    videoLayout_->removeWidget(videoSurface);
    if (restoreState.originalParent != nullptr && restoreState.originalLayout != nullptr &&
        restoreState.layoutIndex >= 0) {
        videoSurface->setParent(restoreState.originalParent);
        restoreState.originalLayout->insertWidget(
            restoreState.layoutIndex, videoSurface, restoreState.layoutStretch
        );
        videoSurface->setSizePolicy(restoreState.sizePolicy);
    } else {
        // 原窗口提前销毁时不能安全恢复布局，交由当前窗口的父子关系完成资源释放。
        videoSurface->hide();
    }

    if (videoWidget != nullptr) {
        videoWidget->setFullscreenSurfaceMode(false, restoreState.statusLabelWasVisible);
        videoSurface->setVisible(restoreState.surfaceWasVisible);
        if (restoreState.originalLayout != nullptr) {
            restoreState.originalLayout->activate();
        }
        videoWidget->updateGeometry();
    }

    activeVideoSurface_ = nullptr;
    clearRestoreState();

    // 先让 MainWindow 在仍被黑色全屏窗口覆盖时恢复，最后隐藏顶层窗口，避免暴露桌面中间帧。
    emit fullscreenExited(videoWidget);
    hide();
    transitionState_ = TransitionState::Windowed;
}

bool FullscreenVideoWindow::isFullscreenActive() const noexcept
{
    return activeVideoSurface_ != nullptr;
}

void FullscreenVideoWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (controlBar_->isVisible() || isPointerInRevealArea(event->position())) {
        showControlBar(true);
    }

    QWidget::mouseMoveEvent(event);
}

void FullscreenVideoWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        exitFullscreen();
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void FullscreenVideoWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        exitFullscreen();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void FullscreenVideoWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // WA_OpaquePaintEvent 要求当前控件覆盖每个像素；显式填黑可避免系统背景穿透。
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}

void FullscreenVideoWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionControlBar();
}

void FullscreenVideoWindow::closeEvent(QCloseEvent *event)
{
    exitFullscreen();
    event->accept();
}

QScreen *FullscreenVideoWindow::screenForVideoWidget(const VideoWidget *videoWidget) const
{
    const QPoint globalCenter =
        videoWidget->mapToGlobal(videoWidget->rect().center());
    if (QScreen *screen = QGuiApplication::screenAt(globalCenter); screen != nullptr) {
        return screen;
    }

    return videoWidget->screen() != nullptr ? videoWidget->screen()
                                             : QGuiApplication::primaryScreen();
}

bool FullscreenVideoWindow::isPointerInRevealArea(const QPointF &position) const noexcept
{
    return position.y() >= height() - kControlBarRevealHeight;
}

void FullscreenVideoWindow::showControlBar(bool restartTimer)
{
    if (!isFullscreenActive()) {
        return;
    }

    controlBar_->show();
    positionControlBar();
    controlBar_->raise();
    unsetCursor();

    if (restartTimer && !controlBar_->underMouse()) {
        autoHideTimer_->start(kControlBarAutoHideDelayMs);
    }
}

void FullscreenVideoWindow::scheduleControlBarHide()
{
    if (isFullscreenActive() && controlBar_->isVisible() && !controlBar_->underMouse()) {
        autoHideTimer_->start(kControlBarAutoHideDelayMs);
    }
}

void FullscreenVideoWindow::hideControlBar()
{
    if (!isFullscreenActive() || controlBar_->underMouse()) {
        return;
    }

    controlBar_->hide();
    setCursor(Qt::BlankCursor);
}

void FullscreenVideoWindow::positionControlBar()
{
    controlBar_->adjustSize();
    const QSize controlBarSize = controlBar_->sizeHint();
    const int x = (width() - controlBarSize.width()) / 2;
    const int y = height() - controlBarSize.height() - kControlBarBottomMargin;
    controlBar_->setGeometry(x, qMax(0, y), controlBarSize.width(), controlBarSize.height());
}

void FullscreenVideoWindow::clearRestoreState()
{
    restoreState_ = VideoSurfaceRestoreState{};
}


const char *FullscreenVideoWindow::transitionStateName() const noexcept
{
    switch (transitionState_) {
    case TransitionState::Windowed:
        return "windowed";
    case TransitionState::Entering:
        return "entering";
    case TransitionState::Fullscreen:
        return "fullscreen";
    case TransitionState::Exiting:
        return "exiting";
    }

    return "unknown";
}
