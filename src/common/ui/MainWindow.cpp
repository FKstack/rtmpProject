#include "ui/MainWindow.h"

#include <utility>

#include <QAction>
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>

#if defined(Q_OS_WIN)
#include <QWindow>
#include <qt_windows.h>
#endif

#include "ui/FullscreenVideoWindow.h"
#include "logging/UserMessageService.h"
#include "media/PlaybackTypes.h"
#include "ui/LogPanel.h"
#include "ui/VideoGridWidget.h"
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

MainWindow::MainWindow(QWidget *parent)
    : MainWindow(RendererPreference::Cpu, parent)
{
}

MainWindow::MainWindow(
    RendererPreference rendererPreference,
    QWidget *parent
)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PC 端多路 RTMP 视频显示"));
    resize(1280, 720);

    videoToolBar_ = addToolBar(tr("视频操作"));
    videoToolBar_->setObjectName(QStringLiteral("videoToolBar"));
    videoToolBar_->setMovable(false);
    videoToolBar_->setFloatable(false);

    addVideoAction_ = videoToolBar_->addAction(tr("添加新的连接"));
    addVideoAction_->setObjectName(QStringLiteral("addConnectionAction"));

    centralStack_ = new QStackedWidget(this);
    emptyPage_ = new QWidget(centralStack_);
    auto *emptyLayout = new QVBoxLayout(emptyPage_);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto *emptyTitle = new QLabel(tr("尚未添加 RTMP 摄像头连接"), emptyPage_);
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyTitle->setObjectName(QStringLiteral("emptyConnectionsTitle"));
    emptyAddButton_ = new QPushButton(tr("添加新的连接"), emptyPage_);
    emptyAddButton_->setObjectName(QStringLiteral("emptyAddConnectionButton"));
    emptyAddButton_->setMinimumSize(200, 52);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyAddButton_, 0, Qt::AlignCenter);

    videoGrid_ = new VideoGridWidget(rendererPreference, centralStack_);
    centralStack_->addWidget(emptyPage_);
    centralStack_->addWidget(videoGrid_);
    setCentralWidget(centralStack_);

    logPanel_ = new LogPanel(this);
    logDockWidget_ = new QDockWidget(tr("事件消息"), this);
    logDockWidget_->setObjectName(QStringLiteral("logDockWidget"));
    logDockWidget_->setAllowedAreas(
        Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea
    );
    logDockWidget_->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, logDockWidget_);

    QMenu *viewMenu = menuBar()->addMenu(tr("视图"));
    showLogAction_ = logDockWidget_->toggleViewAction();
    showLogAction_->setText(tr("事件消息"));
    showLogAction_->setObjectName(QStringLiteral("showLogAction"));
    viewMenu->addAction(showLogAction_);
    monitoringWallAction_ = viewMenu->addAction(tr("监控墙模式"));
    monitoringWallAction_->setObjectName(
        QStringLiteral("monitoringWallAction")
    );
    monitoringWallAction_->setCheckable(true);
    monitoringWallAction_->setShortcut(QKeySequence(Qt::Key_F11));
    monitoringWallAction_->setShortcutContext(Qt::WindowShortcut);
    // 监控模式默认优先使用垂直空间；日志继续收集并可从“视图”菜单随时呼出。
    logDockWidget_->hide();

    fullscreenVideoWindow_ = new FullscreenVideoWindow(
        rendererPreference, this
    );

#if defined(Q_OS_LINUX)
    // EGLFS 只允许一个 GL 顶层窗口：全屏复用主画布，不创建第二个
    // QOpenGLWidget 顶层窗口。
    fullscreenPresentationMode_ =
        fullscreenPresentationModeForQpa(QGuiApplication::platformName());
#endif

    connect(
        addVideoAction_, &QAction::triggered,
        this, &MainWindow::addConnectionRequested
    );
    connect(
        emptyAddButton_, &QPushButton::clicked,
        this, &MainWindow::addConnectionRequested
    );
    connect(
        monitoringWallAction_, &QAction::toggled,
        this, &MainWindow::setMonitoringWallMode
    );
    connect(videoGrid_, &VideoGridWidget::videoWidgetCountChanged,
            this, [this](int) {
                updateAddVideoAction();
                updateCentralPage();
            });
    connect(videoGrid_, &VideoGridWidget::gridInteractionStateChanged,
            this, [this](VideoGridWidget::GridInteractionState) {
                updateAddVideoAction();
            });
    connect(videoGrid_, &VideoGridWidget::maximumVideoWidgetCountReached,
            this, [this] {
                statusBar()->showMessage(
                    tr("已达到最多 %1 个视频窗口")
                        .arg(VideoGridWidget::kMaximumVideoWidgetCount),
                    3000
                );
                updateAddVideoAction();
            });

    connect(videoGrid_, &VideoGridWidget::fullscreenRequested,
            this, &MainWindow::handleFullscreenRequest);
    connect(fullscreenVideoWindow_, &FullscreenVideoWindow::fullscreenExitStarted,
            videoGrid_, &VideoGridWidget::notifyFullscreenExitStarted);
    connect(fullscreenVideoWindow_,
            &FullscreenVideoWindow::fullscreenRestoreRequested,
            this, &MainWindow::beginRestoreBehindFullscreen);
    connect(fullscreenVideoWindow_, &FullscreenVideoWindow::fullscreenExited,
            this, [this](VideoWidget *videoWidget) {
                QObject::disconnect(fullscreenRestorePaintConnection_);
                fullscreenRestorePaintConnection_ = {};
                videoGrid_->notifyFullscreenExited(videoWidget);
                restoreAfterFullscreen();
            });

    updateAddVideoAction();
    updateCentralPage();
}

MainWindow::~MainWindow()
{
    // 析构期间不应因恢复信号重新显示正在销毁的主窗口。
    wasVisibleBeforeFullscreen_ = false;
    QObject::disconnect(fullscreenRestorePaintConnection_);
    fullscreenRestorePaintConnection_ = {};
    fullscreenVideoWindow_->exitFullscreen();
    fullscreenVideoWindow_->completeExitTransition();
}

VideoWidget *MainWindow::videoWidgetAt(int index) const noexcept
{
    return videoGrid_ != nullptr ? videoGrid_->videoWidgetAt(index) : nullptr;
}

VideoWidget *MainWindow::primaryVideoWidget() const noexcept
{
    return videoWidgetAt(0);
}

VideoWidget *MainWindow::addConnectionWidget(const QString &displayName)
{
    VideoWidget *videoWidget = videoGrid_->addVideoWidget(displayName);
    updateAddVideoAction();
    updateCentralPage();
    return videoWidget;
}

bool MainWindow::removeConnectionWidget(VideoWidget *videoWidget)
{
    const bool removed = videoGrid_->removeVideoWidget(videoWidget);
    updateAddVideoAction();
    updateCentralPage();
    return removed;
}

void MainWindow::bindVideoStream(
    VideoWidget *videoWidget,
    StreamId streamId,
    std::shared_ptr<LatestFrameMailbox> mailbox
)
{
    videoGrid_->bindVideoStream(videoWidget, streamId, std::move(mailbox));
}

int MainWindow::videoWidgetCount() const noexcept
{
    return videoGrid_ != nullptr ? videoGrid_->videoWidgetCount() : 0;
}

RenderRuntimeMetrics MainWindow::rendererRuntimeMetrics() const
{
    if (fullscreenVideoWindow_ != nullptr &&
        fullscreenVideoWindow_->isFullscreenActive()) {
        return fullscreenVideoWindow_->rendererRuntimeMetrics();
    }
    return videoGrid_->rendererRuntimeMetrics();
}

void MainWindow::setUserMessageService(UserMessageService *service)
{
    if (logConnection_) {
        disconnect(logConnection_);
        logConnection_ = {};
    }
    if (service != nullptr) {
        logConnection_ = connect(
            service, &UserMessageService::messageAdded,
            logPanel_, &LogPanel::appendMessage
        );
    }
}

void MainWindow::updateDeviceStatus(
    VideoWidget *videoWidget,
    DeviceStatus status,
    UserFailureReason reason
)
{
    if (videoWidget == nullptr) {
        return;
    }
    switch (status) {
    case DeviceStatus::Disconnected:
        videoWidget->clearFrame();
        videoWidget->setStatusText(tr("已断开"));
        break;
    case DeviceStatus::Connecting:
        videoWidget->clearFrame();
        videoWidget->setStatusText(tr("正在连接..."));
        break;
    case DeviceStatus::Playing:
        videoWidget->setStatusText(tr("正在播放或等待画面..."));
        videoWidget->showFrame();
        break;
    case DeviceStatus::Reconnecting:
        videoWidget->clearFrame();
        videoWidget->setStatusText(tr("连接中断，正在重连..."));
        break;
    case DeviceStatus::Error:
        videoWidget->clearFrame();
        if (reason == UserFailureReason::AuthenticationFailed) {
            videoWidget->setStatusText(tr("设备验证失败，请检查设备信息"));
        } else if (reason == UserFailureReason::MediaUnavailable) {
            videoWidget->setStatusText(tr("暂时无法获取设备画面"));
        } else {
            videoWidget->setStatusText(tr("设备连接失败，请检查网络连接"));
        }
        break;
    }
}

QDockWidget *MainWindow::logDockWidget() const noexcept
{
    return logDockWidget_;
}

LogPanel *MainWindow::logPanel() const noexcept
{
    return logPanel_;
}

bool MainWindow::isMonitoringWallMode() const noexcept
{
    return monitoringWallMode_;
}

void MainWindow::setMonitoringWallMode(bool enabled)
{
    if (monitoringWallMode_ == enabled) {
        return;
    }

    monitoringWallMode_ = enabled;
    if (monitoringWallAction_ != nullptr &&
        monitoringWallAction_->isChecked() != enabled) {
        const QSignalBlocker blocker(monitoringWallAction_);
        monitoringWallAction_->setChecked(enabled);
    }

    if (enabled) {
        geometryBeforeMonitoringWall_ = saveGeometry();
        windowStateBeforeMonitoringWall_ =
            windowState() & ~Qt::WindowFullScreen;
        menuVisibleBeforeMonitoringWall_ = menuBar()->isVisible();
        toolbarVisibleBeforeMonitoringWall_ =
            videoToolBar_ != nullptr && videoToolBar_->isVisible();
        QStatusBar *existingStatusBar = findChild<QStatusBar *>(
            QString(), Qt::FindDirectChildrenOnly
        );
        statusBarExistedBeforeMonitoringWall_ = existingStatusBar != nullptr;
        statusBarVisibleBeforeMonitoringWall_ =
            existingStatusBar != nullptr && existingStatusBar->isVisible();
        logVisibleBeforeMonitoringWall_ =
            logDockWidget_ != nullptr && logDockWidget_->isVisible();

        videoGrid_->setMonitoringWallMode(true);
        menuBar()->hide();
        if (videoToolBar_ != nullptr) {
            videoToolBar_->hide();
        }
        if (existingStatusBar != nullptr) {
            existingStatusBar->hide();
        }
        if (logDockWidget_ != nullptr) {
            logDockWidget_->hide();
        }
        showFullScreen();
        return;
    }

    videoGrid_->setMonitoringWallMode(false);
    showNormal();
    if (!geometryBeforeMonitoringWall_.isEmpty()) {
        restoreGeometry(geometryBeforeMonitoringWall_);
    }
    if (windowStateBeforeMonitoringWall_.testFlag(Qt::WindowMaximized)) {
        showMaximized();
    } else {
        setWindowState(windowStateBeforeMonitoringWall_);
        show();
    }
    menuBar()->setVisible(menuVisibleBeforeMonitoringWall_);
    if (videoToolBar_ != nullptr) {
        videoToolBar_->setVisible(toolbarVisibleBeforeMonitoringWall_);
    }
    if (statusBarExistedBeforeMonitoringWall_) {
        if (QStatusBar *existingStatusBar = findChild<QStatusBar *>(
                QString(), Qt::FindDirectChildrenOnly
            ); existingStatusBar != nullptr) {
            existingStatusBar->setVisible(statusBarVisibleBeforeMonitoringWall_);
        }
    }
    if (logDockWidget_ != nullptr) {
        logDockWidget_->setVisible(logVisibleBeforeMonitoringWall_);
    }
}

bool MainWindow::event(QEvent *event)
{
    const bool handled = QMainWindow::event(event);
#if defined(Q_OS_WIN)
    if (event != nullptr &&
        (event->type() == QEvent::WinIdChange ||
         (event->type() == QEvent::WindowStateChange && isFullScreen()))) {
        applyWindowsOpenGlFullscreenBorder(this);
    }
#else
    Q_UNUSED(event);
#endif
    return handled;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event != nullptr && event->key() == Qt::Key_Escape &&
        monitoringWallMode_) {
        setMonitoringWallMode(false);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::updateAddVideoAction()
{
    if (addVideoAction_ == nullptr) {
        return;
    }

    const int widgetCount = videoGrid_->videoWidgetCount();
    const bool maximumReached =
        widgetCount >= VideoGridWidget::kMaximumVideoWidgetCount;
    addVideoAction_->setEnabled(videoGrid_->canAddVideoWidget());

    if (maximumReached) {
        addVideoAction_->setToolTip(
            tr("已达到最多 %1 个视频窗口")
                .arg(VideoGridWidget::kMaximumVideoWidgetCount)
        );
    } else if (videoGrid_->interactionState() !=
               VideoGridWidget::GridInteractionState::Idle) {
        addVideoAction_->setToolTip(tr("布局动画或全屏期间暂不可添加"));
    } else {
        addVideoAction_->setToolTip(
            tr("添加视频窗口（当前 %1/%2）")
                .arg(widgetCount)
                .arg(VideoGridWidget::kMaximumVideoWidgetCount)
        );
    }
}

void MainWindow::updateCentralPage()
{
    if (centralStack_ == nullptr || emptyPage_ == nullptr ||
        videoGrid_ == nullptr) {
        return;
    }
    centralStack_->setCurrentWidget(
        videoGrid_->videoWidgetCount() == 0
            ? emptyPage_
            : static_cast<QWidget *>(videoGrid_)
    );
}

void MainWindow::handleFullscreenRequest(VideoWidget *videoWidget)
{
    if (videoGrid_->interactionState() !=
            VideoGridWidget::GridInteractionState::EnteringFullscreen ||
        fullscreenVideoWindow_->isFullscreenActive()) {
        videoGrid_->notifyFullscreenEntryResult(videoWidget, false);
        return;
    }

    if (fullscreenPresentationMode_ ==
            FullscreenPresentationMode::ReuseMainCanvas) {
        // EGLFS：不隐藏主窗口、不创建第二画布，只切换主画布 Snapshot/FPS。
        const bool entered = videoGrid_->enterInCanvasFullscreen(videoWidget);
        videoGrid_->notifyFullscreenEntryResult(videoWidget, entered);
        return;
    }

    wasVisibleBeforeFullscreen_ = isVisible();
    if (wasVisibleBeforeFullscreen_) {
        // 先隐藏主 OpenGL 顶层窗口，再显示单路全屏窗口，避免 Windows DWM
        // 在两个 OpenGL 顶层窗口重叠期间合成历史表面。
        hide();
    }
    const bool entered = fullscreenVideoWindow_->enterFullscreen(videoWidget);
    videoGrid_->notifyFullscreenEntryResult(videoWidget, entered);

    if (!entered) {
        restoreAfterFullscreen();
    }
}

void MainWindow::restoreAfterFullscreen()
{
    if (!wasVisibleBeforeFullscreen_) {
        return;
    }

    wasVisibleBeforeFullscreen_ = false;
    raise();
    activateWindow();
}

void MainWindow::beginRestoreBehindFullscreen(VideoWidget *videoWidget)
{
    Q_UNUSED(videoWidget);
    QObject::disconnect(fullscreenRestorePaintConnection_);
    fullscreenRestorePaintConnection_ = {};

    // 网格在 raster 过渡图遮挡期间保持 ExitingFullscreen，但先恢复完整
    // Snapshot。主画布真正 paint 后才允许隐藏遮罩并激活主窗口。
    videoGrid_->refreshRenderSnapshot();
    if (!wasVisibleBeforeFullscreen_) {
        fullscreenVideoWindow_->completeExitTransition();
        return;
    }

    fullscreenRestorePaintConnection_ = connect(
        videoGrid_, &VideoGridWidget::surfacePresented, this, [this] {
            QObject::disconnect(fullscreenRestorePaintConnection_);
            fullscreenRestorePaintConnection_ = {};
            // 让本次 CPU backing store / QOpenGLWidget composition 先完成提交，
            // 下一事件循环再揭开 raster 遮罩，避免“paint 已返回但窗口尚未合成”。
            QTimer::singleShot(
                0, fullscreenVideoWindow_,
                &FullscreenVideoWindow::completeExitTransition
            );
        }
    );

    if (monitoringWallMode_) {
        showFullScreen();
    } else if (windowState().testFlag(Qt::WindowMaximized)) {
        showMaximized();
    } else {
        show();
    }
    // show()/showFullScreen() 在 Windows 上可能隐式改变 Z 序；在事件循环
    // 真正绘制主画布前重新把纯 raster 过渡窗口放在最前，避免露出白色 backing store。
    fullscreenVideoWindow_->raise();
    // showEvent 与显式刷新共同保证 CPU/OpenGL 两个后端都产生首个呈现信号。
    videoGrid_->refreshRenderSnapshot();
}
