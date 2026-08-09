#include "ui/FullscreenVideoWindow.h"

#include <algorithm>
#include <utility>

#include <QAbstractAnimation>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QRunnable>
#include <QSaveFile>
#include <QScreen>
#include <QShortcut>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <QWindow>
#include <qt_windows.h>
#endif

#include "ui/FullscreenControlBar.h"
#include "ui/VideoWidget.h"

namespace {

QString sanitizeFilenameComponent(QString value)
{
    value = value.trimmed();
    value.replace(
        QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")),
        QStringLiteral("_")
    );
    value.remove(QRegularExpression(QStringLiteral("[ .]+$")));
    if (value.size() > 80) {
        value.truncate(80);
        value.remove(QRegularExpression(QStringLiteral("[ .]+$")));
    }

    static const QRegularExpression reservedName(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption
    );
    if (reservedName.match(value).hasMatch()) {
        value.prepend(QLatin1Char('_'));
    }
    return value;
}

#if defined(Q_OS_WIN)
void applyWindowsOpenGlFullscreenBorder(QWidget *widget)
{
    if (widget == nullptr || widget->windowHandle() == nullptr) {
        return;
    }

    const HWND window = reinterpret_cast<HWND>(widget->windowHandle()->winId());
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if ((style & WS_BORDER) == 0) {
        SetWindowLongPtrW(window, GWL_STYLE, style | WS_BORDER);
    }
}
#endif

} // namespace

FullscreenVideoWindow::FullscreenVideoWindow(
    RendererPreference rendererPreference,
    QWidget *parent
)
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

    canvasHost_ = new VideoCanvasHost(rendererPreference, this);
    canvasHost_->setObjectName(QStringLiteral("fullscreenVideoCanvas"));
    canvasHost_->setTargetFps(30);
    canvasHost_->setMouseTracking(true);
    // 临时全屏画布没有鼠标交互职责。让事件穿透到全屏窗口，避免
    // QOpenGLWidget/CPU backend 在无按键移动时吞掉 MouseMove。
    canvasHost_->setAttribute(Qt::WA_TransparentForMouseEvents);
    videoLayout_->addWidget(canvasHost_);

    revealZone_ = new QWidget(this);
    revealZone_->setObjectName(QStringLiteral("fullscreenControlRevealZone"));
    revealZone_->setAttribute(Qt::WA_StyledBackground, false);
    revealZone_->setAttribute(Qt::WA_NoSystemBackground);
    revealZone_->setAutoFillBackground(false);
    revealZone_->setStyleSheet(QStringLiteral("background: transparent;"));
    revealZone_->setMouseTracking(true);

    // 控制栏必须受底部热区裁剪。若直接把它动画到全屏顶层窗口边界外，
    // Windows DWM 与 QOpenGLWidget 合成时可能把越界表面翻转到窗口顶部。
    controlBar_ = new FullscreenControlBar(revealZone_);
    controlBar_->hide();

    controlBarAnimation_ = new QPropertyAnimation(controlBar_, "geometry", this);
    controlBarAnimation_->setObjectName(QStringLiteral("fullscreenControlBarAnimation"));
    controlBarAnimation_->setDuration(kControlBarAnimationDurationMs);
    controlBarAnimation_->setEasingCurve(QEasingCurve::OutCubic);

    hideDelayTimer_ = new QTimer(this);
    hideDelayTimer_->setSingleShot(true);

    cursorHideTimer_ = new QTimer(this);
    cursorHideTimer_->setObjectName(QStringLiteral("fullscreenCursorHideTimer"));
    cursorHideTimer_->setSingleShot(true);

    screenshotShortcut_ = new QShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+S")), this
    );
    screenshotShortcut_->setContext(Qt::WindowShortcut);

    screenshotToast_ = new QFrame(this);
    screenshotToast_->setObjectName(QStringLiteral("screenshotToast"));
    screenshotToast_->setAttribute(Qt::WA_StyledBackground);
    screenshotToast_->setStyleSheet(QStringLiteral(
        "QFrame#screenshotToast { background: rgba(20, 20, 20, 225); "
        "border: 1px solid rgba(255, 255, 255, 90); border-radius: 8px; } "
        "QLabel { color: white; }"
    ));
    auto *toastLayout = new QHBoxLayout(screenshotToast_);
    toastLayout->setContentsMargins(10, 10, 12, 10);
    toastLayout->setSpacing(10);
    screenshotThumbnailLabel_ = new QLabel(screenshotToast_);
    screenshotThumbnailLabel_->setObjectName(
        QStringLiteral("screenshotThumbnailLabel")
    );
    screenshotThumbnailLabel_->setAlignment(Qt::AlignCenter);
    screenshotMessageLabel_ = new QLabel(screenshotToast_);
    screenshotMessageLabel_->setObjectName(
        QStringLiteral("screenshotMessageLabel")
    );
    screenshotMessageLabel_->setWordWrap(true);
    screenshotMessageLabel_->setMaximumWidth(360);
    toastLayout->addWidget(screenshotThumbnailLabel_);
    toastLayout->addWidget(screenshotMessageLabel_);
    screenshotToast_->hide();

    screenshotToastTimer_ = new QTimer(this);
    screenshotToastTimer_->setSingleShot(true);

    transitionOverlay_ = new QLabel(this);
    transitionOverlay_->setObjectName(QStringLiteral("fullscreenExitTransitionOverlay"));
    transitionOverlay_->setAlignment(Qt::AlignCenter);
    transitionOverlay_->setScaledContents(true);
    transitionOverlay_->setStyleSheet(QStringLiteral("background: black;"));
    transitionOverlay_->hide();

    exitTransitionTimer_ = new QTimer(this);
    exitTransitionTimer_->setSingleShot(true);

    connect(hideDelayTimer_, &QTimer::timeout, this, [this] {
        hideControlBar(true);
    });
    connect(controlBarAnimation_, &QPropertyAnimation::finished, this, [this] {
        if (!controlBarTargetVisible_) {
            controlBar_->hide();
            scheduleCursorHide();
        }
    });
    connect(cursorHideTimer_, &QTimer::timeout, this, [this] {
        const VideoWidget *videoWidget = restoreState_.videoWidget;
        if (transitionState_ == TransitionState::Fullscreen &&
            videoWidget != nullptr && videoWidget->isFrameVisible() &&
            !controlBar_->isVisible() && !pointerInRevealZone_ &&
            !pointerInControlBar_) {
            setCursor(Qt::BlankCursor);
        }
    });
    connect(screenshotToastTimer_, &QTimer::timeout,
            screenshotToast_, &QWidget::hide);
    connect(exitTransitionTimer_, &QTimer::timeout,
            this, &FullscreenVideoWindow::completeExitTransition);
    connect(controlBar_, &FullscreenControlBar::exitRequested,
            this, &FullscreenVideoWindow::exitFullscreen);
    connect(controlBar_, &FullscreenControlBar::pointerEntered, this, [this] {
        pointerInControlBar_ = true;
        hideDelayTimer_->stop();
        cursorHideTimer_->stop();
        unsetCursor();
    });
    connect(controlBar_, &FullscreenControlBar::pointerLeft, this, [this] {
        pointerInControlBar_ = false;
        scheduleControlBarHide();
    });
    connect(controlBar_, &FullscreenControlBar::muteRequested, this, [this] {
        if (restoreState_.videoWidget != nullptr) {
            emit muteRequested(restoreState_.videoWidget);
        }
        showControlBar(true);
    });
    connect(controlBar_, &FullscreenControlBar::screenshotRequested,
            this, &FullscreenVideoWindow::requestScreenshot);
    connect(screenshotShortcut_, &QShortcut::activated,
            this, &FullscreenVideoWindow::requestScreenshot);

    // 全屏鼠标事件大多由 CPU/OpenGL 子画布或 Toast 接收。应用级过滤仅在
    // 本窗口处于 Fullscreen 状态且事件坐标落在本窗口内时工作。
    if (QCoreApplication *application = QCoreApplication::instance();
        application != nullptr) {
        application->installEventFilter(this);
    }
}

FullscreenVideoWindow::~FullscreenVideoWindow()
{
    if (QCoreApplication *application = QCoreApplication::instance();
        application != nullptr) {
        application->removeEventFilter(this);
    }
    exitFullscreen();
    completeExitTransition();
}

bool FullscreenVideoWindow::enterFullscreen(VideoWidget *videoWidget)
{
    if (transitionState_ != TransitionState::Windowed || videoWidget == nullptr ||
        videoWidget->isHidden() ||
        videoWidget->streamId() == kInvalidStreamId ||
        videoWidget->frameMailbox() == nullptr) {
        return false;
    }

    transitionState_ = TransitionState::Entering;

    QObject::disconnect(renderStateConnection_);
    renderStateConnection_ = {};
    exitTransitionTimer_->stop();
    stopControlBarMotion();
    screenshotToastTimer_->stop();
    screenshotToast_->hide();
    latestScreenshotPath_.clear();
    latestScreenshotThumbnail_ = {};
    transitionOverlay_->hide();
    transitionOverlay_->clear();
    exitingVideoWidget_ = nullptr;
    pointerInRevealZone_ = false;
    pointerInControlBar_ = false;
    lastFrameVisible_ = false;
    canvasHost_->setSnapshot(RenderSnapshot {});
    canvasHost_->show();
    controlBar_->setDeviceName({});
    controlBar_->setStreamInfo({});

    QScreen *targetScreen = screenForVideoWidget(videoWidget);

    restoreState_.videoWidget = videoWidget;
    canvasHost_->registerStream(
        videoWidget->streamId(), videoWidget->frameMailbox()
    );

    controlBar_->setDeviceName(videoWidget->deviceName());
    controlBar_->setStreamInfo(videoWidget->statusText());
    renderStateConnection_ = connect(
        videoWidget,
        &VideoWidget::renderStateChanged,
        this,
        &FullscreenVideoWindow::refreshActivePresentation
    );

    if (targetScreen != nullptr) {
        setScreen(targetScreen);
        setGeometry(targetScreen->geometry());
    }

    refreshRenderSnapshot();
    videoLayout_->activate();
    showFullScreen();
    transitionState_ = TransitionState::Fullscreen;
    revealZone_->show();
    raise();
    activateWindow();
    setFocus();
    updateRevealZoneGeometry();
    refreshActivePresentation();
    emit fullscreenEntered(videoWidget);
    return true;
}

void FullscreenVideoWindow::exitFullscreen()
{
    if (transitionState_ != TransitionState::Fullscreen || !isFullscreenActive()) {
        return;
    }

    QPointer<VideoWidget> videoWidget = restoreState_.videoWidget;
    beginExitTransition(videoWidget);
}

void FullscreenVideoWindow::completeExitTransition()
{
    if (transitionState_ != TransitionState::Exiting) {
        return;
    }

    exitTransitionTimer_->stop();
    transitionOverlay_->hide();
    transitionOverlay_->clear();
    hide();
    transitionState_ = TransitionState::Windowed;

    QPointer<VideoWidget> videoWidget = exitingVideoWidget_;
    exitingVideoWidget_ = nullptr;
    emit fullscreenExited(videoWidget);
}

void FullscreenVideoWindow::setScreenshotOutputDirectory(QString directory)
{
    screenshotOutputDirectory_ = QDir::cleanPath(directory.trimmed());
    if (directory.trimmed().isEmpty()) {
        screenshotOutputDirectory_.clear();
    }
}

bool FullscreenVideoWindow::isFullscreenActive() const noexcept
{
    return transitionState_ != TransitionState::Windowed;
}

RenderRuntimeMetrics FullscreenVideoWindow::rendererRuntimeMetrics() const
{
    return canvasHost_->runtimeMetrics();
}

bool FullscreenVideoWindow::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
#if defined(Q_OS_WIN)
    if (event != nullptr &&
        (event->type() == QEvent::WinIdChange ||
         (event->type() == QEvent::WindowStateChange && isFullScreen()))) {
        // Qt 官方 Windows workaround：全屏 OpenGL 顶层窗口保留 WS_BORDER，
        // 让 DWM 能正确合成应用内窗口及 NVIDIA 等外部覆盖层。
        applyWindowsOpenGlFullscreenBorder(this);
    }
#else
    Q_UNUSED(event);
#endif
    return handled;
}

bool FullscreenVideoWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event != nullptr && transitionState_ == TransitionState::Fullscreen) {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease: {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPointF localPosition = mapFromGlobal(
                mouseEvent->globalPosition().toPoint()
            );
            if (rect().contains(localPosition.toPoint())) {
                handlePointerActivity(localPosition);
            }
            break;
        }
        case QEvent::MouseButtonDblClick: {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPointF localPosition = mapFromGlobal(
                mouseEvent->globalPosition().toPoint()
            );
            if (mouseEvent->button() == Qt::LeftButton &&
                rect().contains(localPosition.toPoint())) {
                exitFullscreen();
                return true;
            }
            break;
        }
        case QEvent::Wheel:
        case QEvent::Enter: {
            const QPoint localPosition = mapFromGlobal(QCursor::pos());
            if (rect().contains(localPosition)) {
                handlePointerActivity(localPosition);
            }
            break;
        }
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void FullscreenVideoWindow::mouseMoveEvent(QMouseEvent *event)
{
    handlePointerActivity(event->position());

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
    refreshRenderSnapshot();
    updateRevealZoneGeometry();
    positionScreenshotToast();
    updateTransitionOverlayGeometry();
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

bool FullscreenVideoWindow::isPointerInControlBar(const QPointF &position) const noexcept
{
    if (controlBar_ == nullptr || !controlBar_->isVisible()) {
        return false;
    }
    const QPoint topLeft = controlBar_->mapTo(this, QPoint(0, 0));
    return QRect(topLeft, controlBar_->size()).contains(position.toPoint());
}

void FullscreenVideoWindow::handlePointerActivity(const QPointF &position)
{
    if (transitionState_ != TransitionState::Fullscreen) {
        return;
    }

    unsetCursor();
    cursorHideTimer_->stop();
    pointerInRevealZone_ = isPointerInRevealArea(position);
    pointerInControlBar_ = isPointerInControlBar(position);

    if (pointerInRevealZone_ || pointerInControlBar_) {
        showControlBar(true);
        return;
    }

    scheduleControlBarHide();
    if (!controlBar_->isVisible()) {
        scheduleCursorHide();
    }
}

void FullscreenVideoWindow::scheduleCursorHide()
{
    const VideoWidget *videoWidget = restoreState_.videoWidget;
    if (transitionState_ != TransitionState::Fullscreen ||
        videoWidget == nullptr || !videoWidget->isFrameVisible() ||
        controlBar_->isVisible() || pointerInRevealZone_ ||
        pointerInControlBar_) {
        cursorHideTimer_->stop();
        return;
    }
    cursorHideTimer_->start(kCursorHideDelayMs);
}

QRect FullscreenVideoWindow::visibleControlBarGeometry() const
{
    const QSize controlBarSize = controlBar_->sizeHint();
    const int x = (revealZone_->width() - controlBarSize.width()) / 2;
    const int y = revealZone_->height() - controlBarSize.height() -
                  kControlBarBottomMargin;
    return QRect(x, qMax(0, y), controlBarSize.width(), controlBarSize.height());
}

QRect FullscreenVideoWindow::hiddenControlBarGeometry() const
{
    QRect geometry = visibleControlBarGeometry();
    geometry.moveTop(revealZone_->height() + 1);
    return geometry;
}

void FullscreenVideoWindow::showControlBar(bool animated)
{
    if (transitionState_ != TransitionState::Fullscreen) {
        return;
    }

    hideDelayTimer_->stop();
    cursorHideTimer_->stop();
    unsetCursor();

    const QRect target = visibleControlBarGeometry();
    if (controlBarTargetVisible_ && controlBar_->isVisible() &&
        (controlBarAnimation_->state() == QAbstractAnimation::Running ||
         controlBar_->geometry() == target)) {
        revealZone_->raise();
        controlBar_->raise();
        screenshotToast_->raise();
        return;
    }

    controlBarTargetVisible_ = true;
    controlBarAnimation_->stop();
    if (!controlBar_->isVisible()) {
        controlBar_->setGeometry(
            animated ? hiddenControlBarGeometry() : visibleControlBarGeometry()
        );
        controlBar_->show();
    }
    revealZone_->raise();
    controlBar_->raise();
    screenshotToast_->raise();
    if (!animated || controlBar_->geometry() == target) {
        controlBar_->setGeometry(target);
        return;
    }
    controlBarAnimation_->setStartValue(controlBar_->geometry());
    controlBarAnimation_->setEndValue(target);
    controlBarAnimation_->start();
}

void FullscreenVideoWindow::scheduleControlBarHide(int delayMs)
{
    VideoWidget *videoWidget = restoreState_.videoWidget;
    if (transitionState_ != TransitionState::Fullscreen || videoWidget == nullptr ||
        !videoWidget->isFrameVisible()) {
        showControlBar(false);
        return;
    }
    if (!pointerInRevealZone_ && !pointerInControlBar_) {
        if (!hideDelayTimer_->isActive()) {
            hideDelayTimer_->start(std::max(0, delayMs));
        }
    }
}

void FullscreenVideoWindow::hideControlBar(bool animated)
{
    VideoWidget *videoWidget = restoreState_.videoWidget;
    if (transitionState_ != TransitionState::Fullscreen || videoWidget == nullptr ||
        !videoWidget->isFrameVisible() || pointerInRevealZone_ ||
        pointerInControlBar_) {
        return;
    }

    if (!controlBarTargetVisible_ &&
        (controlBarAnimation_->state() == QAbstractAnimation::Running ||
         !controlBar_->isVisible())) {
        return;
    }

    controlBarTargetVisible_ = false;
    controlBarAnimation_->stop();
    if (!controlBar_->isVisible()) {
        scheduleCursorHide();
        return;
    }
    if (!animated) {
        controlBar_->setGeometry(hiddenControlBarGeometry());
        controlBar_->hide();
        scheduleCursorHide();
        return;
    }
    controlBarAnimation_->setStartValue(controlBar_->geometry());
    controlBarAnimation_->setEndValue(hiddenControlBarGeometry());
    controlBarAnimation_->start();
}

void FullscreenVideoWindow::positionControlBar()
{
    controlBar_->adjustSize();
    controlBarAnimation_->stop();
    controlBar_->setGeometry(
        controlBarTargetVisible_ ? visibleControlBarGeometry()
                                 : hiddenControlBarGeometry()
    );
}

void FullscreenVideoWindow::stopControlBarMotion()
{
    hideDelayTimer_->stop();
    cursorHideTimer_->stop();
    controlBarAnimation_->stop();
    controlBarTargetVisible_ = false;
    controlBar_->hide();
}

void FullscreenVideoWindow::updateRevealZoneGeometry()
{
    revealZone_->setGeometry(
        0, qMax(0, height() - kControlBarRevealHeight),
        width(), qMin(height(), kControlBarRevealHeight)
    );
    positionControlBar();
    revealZone_->raise();
    if (controlBar_->isVisible()) {
        controlBar_->raise();
    }
    if (screenshotToast_->isVisible()) {
        screenshotToast_->raise();
    }
}

void FullscreenVideoWindow::refreshActivePresentation()
{
    VideoWidget *videoWidget = restoreState_.videoWidget;
    if (videoWidget == nullptr) {
        return;
    }

    controlBar_->setDeviceName(videoWidget->deviceName());
    controlBar_->setStreamInfo(videoWidget->statusText());
    refreshRenderSnapshot();

    if (transitionState_ != TransitionState::Fullscreen) {
        return;
    }

    const bool frameVisible = videoWidget->isFrameVisible();
    if (!frameVisible) {
        lastFrameVisible_ = false;
        pointerInRevealZone_ = false;
        stopControlBarMotion();
        controlBarTargetVisible_ = true;
        controlBar_->show();
        positionControlBar();
        controlBar_->raise();
        unsetCursor();
        return;
    }

    if (!lastFrameVisible_) {
        lastFrameVisible_ = true;
        showControlBar(false);
        const QPoint localPointer = mapFromGlobal(QCursor::pos());
        pointerInRevealZone_ = isPointerInRevealArea(localPointer);
        if (!pointerInRevealZone_ && !pointerInControlBar_) {
            scheduleControlBarHide(kFirstFrameHideDelayMs);
        }
    }
}

void FullscreenVideoWindow::refreshRenderSnapshot()
{
    RenderSnapshot snapshot;
    snapshot.logicalCanvasSize = canvasHost_->size();
    snapshot.devicePixelRatio = canvasHost_->devicePixelRatioF();
    VideoWidget *videoWidget = restoreState_.videoWidget;
    if (videoWidget != nullptr && videoWidget->streamId() != kInvalidStreamId) {
        RenderItem item;
        item.streamId = videoWidget->streamId();
        item.tileRect = canvasHost_->rect();
        item.videoViewport = canvasHost_->rect();
        item.displayMode = VideoDisplayMode::Contain;
        item.title = videoWidget->deviceName();
        item.status = videoWidget->statusText();
        item.frameVisible = videoWidget->isFrameVisible();
        item.fullscreen = true;
        snapshot.items.push_back(std::move(item));
    }
    canvasHost_->setSnapshot(std::move(snapshot));
}

void FullscreenVideoWindow::requestScreenshot()
{
    VideoWidget *videoWidget = restoreState_.videoWidget;
    if (transitionState_ != TransitionState::Fullscreen || videoWidget == nullptr) {
        return;
    }

    emit screenshotRequested(videoWidget);
    showControlBar(true);
    if (videoWidget->isFrameVisible() && !pointerInRevealZone_ &&
        !pointerInControlBar_) {
        scheduleControlBarHide(kFirstFrameHideDelayMs);
    }

    if (!videoWidget->isFrameVisible()) {
        const QString reason = tr("暂无可截图画面 · Ctrl+Shift+S");
        showScreenshotToast({}, reason, true);
        emit screenshotFailed(reason);
        return;
    }

    QImage framebuffer = canvasHost_->grabFramebufferImage();
    if (framebuffer.isNull()) {
        const QString reason = tr("截图失败：当前渲染画面不可用");
        showScreenshotToast({}, reason, true);
        emit screenshotFailed(reason);
        return;
    }
    framebuffer = framebuffer.copy();

    const QString directory = resolvedScreenshotDirectory();
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        const QString reason = tr("截图失败：无法创建截图目录");
        showScreenshotToast(framebuffer, reason, true);
        emit screenshotFailed(reason);
        return;
    }

    const QString path = nextScreenshotPath();
    latestScreenshotPath_ = path;
    latestScreenshotThumbnail_ = framebuffer.scaled(
        QSize(320, 180), Qt::KeepAspectRatio, Qt::FastTransformation
    );
    showScreenshotToast(framebuffer, tr("正在保存 · Ctrl+Shift+S"), false);

    QPointer<FullscreenVideoWindow> window(this);
    QThreadPool::globalInstance()->start(QRunnable::create(
        [framebuffer = std::move(framebuffer), path, window]() mutable {
            QString error;
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly)) {
                error = QStringLiteral("无法创建 PNG 文件");
            } else if (!framebuffer.save(&output, "PNG")) {
                output.cancelWriting();
                error = QStringLiteral("PNG 编码失败");
            } else if (!output.commit()) {
                error = QStringLiteral("无法原子提交 PNG 文件");
            }

            if (QCoreApplication *application = QCoreApplication::instance();
                application != nullptr) {
                QMetaObject::invokeMethod(
                    application,
                    [window, path, error] {
                        if (window != nullptr) {
                            window->finishScreenshotSave(path, error);
                        }
                    },
                    Qt::QueuedConnection
                );
            }
        }
    ));
}

QString FullscreenVideoWindow::resolvedScreenshotDirectory() const
{
    if (!screenshotOutputDirectory_.isEmpty()) {
        return screenshotOutputDirectory_;
    }

    QString root = QStandardPaths::writableLocation(
        QStandardPaths::PicturesLocation
    );
    if (!root.isEmpty()) {
        return QDir(root).filePath(QStringLiteral("RtmpMonitor/Screenshots"));
    }
    root = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation
    );
    return root.isEmpty()
        ? QString()
        : QDir(root).filePath(QStringLiteral("Screenshots"));
}

QString FullscreenVideoWindow::nextScreenshotPath()
{
    const QString timestamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz")
    );
    const quint64 sequence = ++screenshotSequence_;
    return QDir(resolvedScreenshotDirectory()).filePath(
        QStringLiteral("%1-%2-%3.png")
            .arg(safeScreenshotBaseName(), timestamp)
            .arg(sequence, 4, 10, QLatin1Char('0'))
    );
}

QString FullscreenVideoWindow::safeScreenshotBaseName() const
{
    const VideoWidget *videoWidget = restoreState_.videoWidget;
    QString base = videoWidget != nullptr
        ? sanitizeFilenameComponent(videoWidget->deviceName())
        : QString();
    if (!base.isEmpty()) {
        return base;
    }

    const StreamId streamId = videoWidget != nullptr
        ? videoWidget->streamId()
        : kInvalidStreamId;
    return streamId == kInvalidStreamId
        ? QStringLiteral("camera")
        : QStringLiteral("camera-%1").arg(streamId);
}

void FullscreenVideoWindow::showScreenshotToast(
    const QImage &thumbnail,
    const QString &message,
    bool autoHide
)
{
    screenshotToastTimer_->stop();
    if (thumbnail.isNull()) {
        screenshotThumbnailLabel_->clear();
        screenshotThumbnailLabel_->hide();
    } else {
        const QPixmap pixmap = QPixmap::fromImage(thumbnail).scaled(
            QSize(320, 180), Qt::KeepAspectRatio, Qt::FastTransformation
        );
        screenshotThumbnailLabel_->setPixmap(pixmap);
        screenshotThumbnailLabel_->setFixedSize(pixmap.size());
        screenshotThumbnailLabel_->show();
    }
    screenshotMessageLabel_->setText(message);
    screenshotToast_->adjustSize();
    positionScreenshotToast();
    screenshotToast_->show();
    screenshotToast_->raise();
    if (autoHide) {
        screenshotToastTimer_->start(kScreenshotToastDurationMs);
    }
}

void FullscreenVideoWindow::positionScreenshotToast()
{
    if (screenshotToast_ == nullptr) {
        return;
    }
    screenshotToast_->adjustSize();
    constexpr int margin = 24;
    screenshotToast_->move(
        qMax(margin, width() - screenshotToast_->width() - margin), margin
    );
}

void FullscreenVideoWindow::finishScreenshotSave(
    const QString &path,
    const QString &error
)
{
    if (!error.isEmpty()) {
        const QString reason = tr("截图失败：%1").arg(error);
        if (transitionState_ == TransitionState::Fullscreen &&
            path == latestScreenshotPath_) {
            showScreenshotToast({}, reason, true);
        }
        emit screenshotFailed(reason);
        return;
    }

    if (transitionState_ == TransitionState::Fullscreen &&
        path == latestScreenshotPath_) {
        showScreenshotToast(
            latestScreenshotThumbnail_,
            tr("已保存：%1\n%2 · Ctrl+Shift+S")
                .arg(QFileInfo(path).fileName(), QFileInfo(path).absolutePath()),
            true
        );
    }
    emit screenshotSaved(path);
}

void FullscreenVideoWindow::beginExitTransition(VideoWidget *videoWidget)
{
    transitionState_ = TransitionState::Exiting;
    exitingVideoWidget_ = videoWidget;

    emit fullscreenExitStarted(videoWidget);

    QImage transitionImage;
    if (videoWidget != nullptr && videoWidget->isFrameVisible()) {
        transitionImage = canvasHost_->grabFramebufferImage();
    }
    if (transitionImage.isNull()) {
        const qreal dpr = devicePixelRatioF();
        transitionImage = QImage(
            QSize(qMax(1, qRound(width() * dpr)),
                  qMax(1, qRound(height() * dpr))),
            QImage::Format_RGB32
        );
        transitionImage.setDevicePixelRatio(dpr);
        transitionImage.fill(Qt::black);
    }

    transitionOverlay_->setPixmap(QPixmap::fromImage(transitionImage));
    updateTransitionOverlayGeometry();
    transitionOverlay_->show();
    transitionOverlay_->raise();

    QObject::disconnect(renderStateConnection_);
    renderStateConnection_ = {};
    stopControlBarMotion();
    revealZone_->hide();
    screenshotToastTimer_->stop();
    screenshotToast_->hide();
    unsetCursor();

    // 先以 raster 最后一帧覆盖整个顶层窗口，再移除可见 GL 子窗口。
    canvasHost_->hide();
    canvasHost_->setSnapshot(RenderSnapshot {});
    if (videoWidget != nullptr && videoWidget->streamId() != kInvalidStreamId) {
        canvasHost_->unregisterStream(videoWidget->streamId());
    }
    clearRestoreState();
    controlBar_->setDeviceName({});
    controlBar_->setStreamInfo({});
    lastFrameVisible_ = false;

    exitTransitionTimer_->start(kExitTransitionTimeoutMs);
    emit fullscreenRestoreRequested(videoWidget);
}

void FullscreenVideoWindow::updateTransitionOverlayGeometry()
{
    if (transitionOverlay_ != nullptr) {
        transitionOverlay_->setGeometry(rect());
    }
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
