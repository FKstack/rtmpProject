#include "ui/FullscreenVideoWindow.h"

#include <utility>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
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
#include <QScreen>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <QWindow>
#include <qt_windows.h>
#endif

#include "ui/FullscreenChromeController.h"
#include "ui/FullscreenControlBar.h"
#include "ui/FullscreenScreenshotService.h"
#include "ui/VideoWidget.h"

namespace {
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
    revealZone_->setMouseTracking(true);

    // 控制栏必须受底部热区裁剪。若直接把它动画到全屏顶层窗口边界外，
    // Windows DWM 与 QOpenGLWidget 合成时可能把越界表面翻转到窗口顶部。
    controlBar_ = new FullscreenControlBar(revealZone_);
    controlBar_->hide();

    chromeController_ = new FullscreenChromeController(
        this, revealZone_, controlBar_, this
    );
    screenshotService_ = new FullscreenScreenshotService(this);

    screenshotShortcut_ = new QShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+S")), this
    );
    screenshotShortcut_->setContext(Qt::WindowShortcut);

    screenshotToast_ = new QFrame(this);
    screenshotToast_->setObjectName(QStringLiteral("screenshotToast"));
    screenshotToast_->setAttribute(Qt::WA_StyledBackground);
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
    transitionOverlay_->hide();

    exitTransitionTimer_ = new QTimer(this);
    exitTransitionTimer_->setSingleShot(true);

    connect(screenshotToastTimer_, &QTimer::timeout,
            screenshotToast_, &QWidget::hide);
    connect(exitTransitionTimer_, &QTimer::timeout,
            this, &FullscreenVideoWindow::completeExitTransition);
    connect(controlBar_, &FullscreenControlBar::exitRequested,
            this, &FullscreenVideoWindow::exitFullscreen);
    connect(controlBar_, &FullscreenControlBar::pointerEntered, this, [this] {
        chromeController_->pointerEnteredControlBar();
    });
    connect(controlBar_, &FullscreenControlBar::pointerLeft, this, [this] {
        chromeController_->pointerLeftControlBar();
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
    connect(screenshotService_, &FullscreenScreenshotService::saveStarted,
            this, [this](const QString &path, const QImage &thumbnail) {
                latestScreenshotPath_ = path;
                showScreenshotToast(
                    thumbnail, tr("正在保存 · Ctrl+Shift+S"), false
                );
            });
    connect(screenshotService_, &FullscreenScreenshotService::saveCompleted,
            this, [this](const QString &path, const QImage &thumbnail) {
                if (transitionState_ == TransitionState::Fullscreen &&
                    path == latestScreenshotPath_) {
                    showScreenshotToast(
                        thumbnail,
                        tr("已保存：%1\n%2 · Ctrl+Shift+S")
                            .arg(QFileInfo(path).fileName(),
                                 QFileInfo(path).absolutePath()),
                        true
                    );
                }
                emit screenshotSaved(path);
            });
    connect(screenshotService_, &FullscreenScreenshotService::saveFailed,
            this, [this](const QString &path, const QString &reason,
                         const QImage &thumbnail) {
                if (transitionState_ == TransitionState::Fullscreen &&
                    (path.isEmpty() || path == latestScreenshotPath_)) {
                    showScreenshotToast(thumbnail, reason, true);
                }
                emit screenshotFailed(reason);
            });

    // 全屏鼠标事件大多由 CPU/OpenGL 子画布或 Toast 接收。应用级过滤仅在
    // 本窗口处于 Fullscreen 状态且事件坐标落在本窗口内时工作。
    if (QCoreApplication *application = QCoreApplication::instance();
        application != nullptr) {
        application->installEventFilter(this);
    }
}

void FullscreenVideoWindow::setDisplayFrameRateRequest(
    const QString &requested,
    int effectiveFps
)
{
    canvasHost_->setDisplayFrameRateRequest(requested, effectiveFps);
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
    const quint64 presentationGeneration = ++presentationGeneration_;

    QObject::disconnect(renderStateConnection_);
    renderStateConnection_ = {};
    exitTransitionTimer_->stop();
    stopControlBarMotion();
    screenshotToastTimer_->stop();
    screenshotToast_->hide();
    latestScreenshotPath_.clear();
    transitionOverlay_->hide();
    transitionOverlay_->clear();
    exitingVideoWidget_ = nullptr;
    chromeController_->resetPointerState();
    chromeController_->setPresentationState(false, false);
    setCursor(Qt::ArrowCursor);
    lastFrameVisible_ = false;
    canvasHost_->setSnapshot(RenderSnapshot {});
    // Keep QOpenGLWidget out of the Windows backing-store composition until a
    // real frame exists.  Compositing an otherwise empty GL surface below the
    // QWidget control overlay can mirror stale overlay pixels at the opposite
    // edge of a fullscreen window.
    canvasHost_->hide();
    controlBar_->setDeviceName({});
    controlBar_->setStreamInfo({});

    QScreen *targetScreen = screenForVideoWidget(videoWidget);

    restoreState_.videoWidget = videoWidget;
    canvasHost_->registerStream(
        videoWidget->streamId(), videoWidget->frameMailbox()
    );

    controlBar_->setDeviceName(videoWidget->deviceName());
    controlBar_->setAudioState(
        videoWidget->audioPlaybackState(), videoWidget->isAudioSelected()
    );
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

    showFullScreen();
    transitionState_ = TransitionState::Fullscreen;
    chromeController_->setPresentationState(
        true, videoWidget->isFrameVisible()
    );
    revealZone_->show();
    raise();
    activateWindow();
    setFocus();
    updateRevealZoneGeometry();
    refreshActivePresentation();
    scheduleCanvasPresentationSync(presentationGeneration);
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
    unsetCursor();
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
    if (event->key() == Qt::Key_M &&
        restoreState_.videoWidget != nullptr) {
        emit muteRequested(restoreState_.videoWidget);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        exitFullscreen();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void FullscreenVideoWindow::setAudioState(
    VideoWidget *videoWidget,
    AudioPlaybackState state,
    bool selected
)
{
    if (restoreState_.videoWidget == videoWidget && controlBar_ != nullptr) {
        controlBar_->setAudioState(state, selected);
    }
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
    synchronizeCanvasPresentation();
    if (transitionState_ == TransitionState::Fullscreen) {
        scheduleCanvasPresentationSync(presentationGeneration_);
    }
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

void FullscreenVideoWindow::handlePointerActivity(const QPointF &position)
{
    chromeController_->handlePointerActivity(position);
}

void FullscreenVideoWindow::showControlBar(bool animated)
{
    chromeController_->show(animated);
    screenshotToast_->raise();
}

void FullscreenVideoWindow::scheduleControlBarHide(int delayMs)
{
    chromeController_->scheduleHide(delayMs);
}

void FullscreenVideoWindow::positionControlBar()
{
    chromeController_->position();
}

void FullscreenVideoWindow::stopControlBarMotion()
{
    chromeController_->stopMotion();
}

void FullscreenVideoWindow::updateRevealZoneGeometry()
{
    chromeController_->updateGeometry();
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

    const bool frameVisible = videoWidget->isFrameVisible();
    controlBar_->setDeviceName(videoWidget->deviceName());
    controlBar_->setStreamInfo(videoWidget->statusText());
    synchronizeCanvasPresentation();
    chromeController_->setPresentationState(
        transitionState_ == TransitionState::Fullscreen, frameVisible
    );

    if (transitionState_ != TransitionState::Fullscreen) {
        return;
    }

    if (!frameVisible) {
        lastFrameVisible_ = false;
        chromeController_->resetPointerState();
        stopControlBarMotion();
        chromeController_->setPresentationState(true, false);
        showControlBar(false);
        controlBar_->raise();
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (!lastFrameVisible_) {
        lastFrameVisible_ = true;
        showControlBar(false);
        const QPoint localPointer = mapFromGlobal(QCursor::pos());
        chromeController_->updatePointerPosition(localPointer);
        if (!chromeController_->pointerInRevealZone() &&
            !chromeController_->pointerInControlBar()) {
            scheduleControlBarHide(kFirstFrameHideDelayMs);
        }
    }
}

void FullscreenVideoWindow::synchronizeCanvasPresentation()
{
    VideoWidget *videoWidget = restoreState_.videoWidget;
    const bool frameVisible = videoWidget != nullptr &&
                              videoWidget->isFrameVisible();
    canvasHost_->setVisible(frameVisible);
    if (frameVisible) {
        // Hidden widgets are excluded from layout geometry.  Make the canvas
        // visible first, then synchronously give the zero-margin layout the
        // current fullscreen rect before deriving RenderSnapshot viewports.
        videoLayout_->invalidate();
        videoLayout_->setGeometry(rect());
        videoLayout_->activate();
    }
    refreshRenderSnapshot();
}

void FullscreenVideoWindow::scheduleCanvasPresentationSync(quint64 generation)
{
    QTimer::singleShot(0, this, [this, generation] {
        if (transitionState_ != TransitionState::Fullscreen ||
            generation != presentationGeneration_) {
            return;
        }
        synchronizeCanvasPresentation();
        updateRevealZoneGeometry();
    });
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
    if (videoWidget->isFrameVisible() &&
        !chromeController_->pointerInRevealZone() &&
        !chromeController_->pointerInControlBar()) {
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

    screenshotService_->save(
        std::move(framebuffer), videoWidget->deviceName(),
        videoWidget->streamId(), screenshotOutputDirectory_
    );
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

void FullscreenVideoWindow::beginExitTransition(VideoWidget *videoWidget)
{
    transitionState_ = TransitionState::Exiting;
    ++presentationGeneration_;
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
    chromeController_->setPresentationState(false, false);
    chromeController_->resetPointerState();
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
