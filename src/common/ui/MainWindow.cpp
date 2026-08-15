#include "ui/MainWindow.h"

#include <utility>

#include <QAction>
#include <QDockWidget>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#if defined(Q_OS_WIN)
#include <QWindow>
#include <qt_windows.h>
#endif

#include "ui/FullscreenVideoWindow.h"
#include "event_center/EventCenterTypes.h"
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
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("RtmpMonitor 监控台"));
    resize(1280, 720);

    videoToolBar_ = addToolBar(tr("视频操作"));
    videoToolBar_->setObjectName(QStringLiteral("videoToolBar"));
    videoToolBar_->setMovable(false);
    videoToolBar_->setFloatable(false);

    auto *brandWidget = new QWidget(videoToolBar_);
    brandWidget->setObjectName(QStringLiteral("productBrand"));
    auto *brandLayout = new QHBoxLayout(brandWidget);
    brandLayout->setContentsMargins(0, 0, 8, 0);
    brandLayout->setSpacing(9);
    auto *brandIcon = new QLabel(brandWidget);
    brandIcon->setObjectName(QStringLiteral("productBrandIcon"));
    brandIcon->setPixmap(
        QIcon(QStringLiteral(":/icons/rtmp-monitor-64.png")).pixmap(28, 28)
    );
    brandIcon->setFixedSize(28, 28);
    auto *brandLabel = new QLabel(tr("RtmpMonitor 监控台"), brandWidget);
    brandLabel->setObjectName(QStringLiteral("productBrandLabel"));
    brandLayout->addWidget(brandIcon);
    brandLayout->addWidget(brandLabel);
    videoToolBar_->addWidget(brandWidget);

    auto *toolbarSpacer = new QWidget(videoToolBar_);
    toolbarSpacer->setObjectName(QStringLiteral("videoToolBarSpacer"));
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    videoToolBar_->addWidget(toolbarSpacer);

    addVideoAction_ = videoToolBar_->addAction(tr("添加连接"));
    addVideoAction_->setObjectName(QStringLiteral("addConnectionAction"));
    savedStreamsAction_ = videoToolBar_->addAction(tr("保存的推流"));
    savedStreamsAction_->setObjectName(QStringLiteral("savedStreamsAction"));
    if (auto *addButton = qobject_cast<QToolButton *>(
            videoToolBar_->widgetForAction(addVideoAction_)
        ); addButton != nullptr) {
        addButton->setProperty("styleRole", QStringLiteral("primary"));
    }

    centralStack_ = new QStackedWidget(this);
    centralStack_->setObjectName(QStringLiteral("centralStack"));
    emptyPage_ = new QWidget(centralStack_);
    emptyPage_->setObjectName(QStringLiteral("emptyConnectionsPage"));
    auto *emptyLayout = new QVBoxLayout(emptyPage_);
    emptyLayout->setContentsMargins(32, 32, 32, 32);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto *emptyCard = new QFrame(emptyPage_);
    emptyCard->setObjectName(QStringLiteral("emptyStateCard"));
    emptyCard->setProperty("styleRole", QStringLiteral("emptyStateCard"));
    emptyCard->setMaximumWidth(520);
    auto *emptyCardLayout = new QVBoxLayout(emptyCard);
    emptyCardLayout->setContentsMargins(42, 38, 42, 38);
    emptyCardLayout->setSpacing(12);

    auto *emptyIcon = new QLabel(emptyCard);
    emptyIcon->setObjectName(QStringLiteral("emptyStateIcon"));
    emptyIcon->setPixmap(
        QIcon(QStringLiteral(":/icons/rtmp-monitor-128.png")).pixmap(76, 76)
    );
    emptyIcon->setFixedSize(76, 76);
    emptyIcon->setAlignment(Qt::AlignCenter);
    auto *emptyTitle = new QLabel(tr("尚未建立视频连接"), emptyCard);
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyTitle->setObjectName(QStringLiteral("emptyConnectionsTitle"));
    auto *emptySubtitle = new QLabel(
        tr("添加 RTMP 摄像头，在统一监控画布中查看实时视频与设备状态。"),
        emptyCard
    );
    emptySubtitle->setObjectName(QStringLiteral("emptyConnectionsSubtitle"));
    emptySubtitle->setAlignment(Qt::AlignCenter);
    emptySubtitle->setWordWrap(true);
    emptyAddButton_ = new QPushButton(tr("添加第一个连接"), emptyCard);
    emptyAddButton_->setObjectName(QStringLiteral("emptyAddConnectionButton"));
    emptyAddButton_->setProperty("styleRole", QStringLiteral("primary"));
    emptyAddButton_->setMinimumSize(208, 44);
    emptyAddButton_->setDefault(true);
    emptyCardLayout->addWidget(emptyIcon, 0, Qt::AlignCenter);
    emptyCardLayout->addWidget(emptyTitle);
    emptyCardLayout->addWidget(emptySubtitle);
    emptyCardLayout->addSpacing(8);
    emptyCardLayout->addWidget(emptyAddButton_, 0, Qt::AlignCenter);
    emptyLayout->addWidget(emptyCard);

    videoGrid_ = new VideoGridWidget(rendererPreference, centralStack_);
    centralStack_->addWidget(emptyPage_);
    centralStack_->addWidget(videoGrid_);
    setCentralWidget(centralStack_);

    // Side docks contain operational controls and therefore own all four
    // corners.  Top/bottom diagnostic docks stay within the central video
    // column instead of shortening a left/right control dock.
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    logPanel_ = new LogPanel(this);
    logDockWidget_ = new QDockWidget(tr("运行消息"), this);
    logDockWidget_->setObjectName(QStringLiteral("logDockWidget"));
    logDockWidget_->setAllowedAreas(
        Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea
    );
    logDockWidget_->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, logDockWidget_);

    menuBar()->setObjectName(QStringLiteral("mainMenuBar"));
    QMenu *viewMenu = menuBar()->addMenu(tr("视图"));
    showLogAction_ = logDockWidget_->toggleViewAction();
    showLogAction_->setText(tr("运行消息"));
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

    eventCenterBadge_ = new QToolButton(this);
    eventCenterBadge_->setObjectName(QStringLiteral("eventCenterStatusBadge"));
    eventCenterBadge_->setText(tr("事件 0"));
    eventCenterBadge_->setAutoRaise(true);
    eventCenterBadge_->setToolTip(tr("打开平台事件中心"));
    statusBar()->addPermanentWidget(eventCenterBadge_);
    connect(eventCenterBadge_, &QToolButton::clicked, this, [this] {
        if (eventCenterDockWidget_ == nullptr) return;
        eventCenterDockWidget_->show();
        eventCenterDockWidget_->raise();
    });

    fullscreenVideoWindow_ = new FullscreenVideoWindow(
        rendererPreference, this
    );
    connect(
        fullscreenVideoWindow_,
        &FullscreenVideoWindow::muteRequested,
        this,
        &MainWindow::audioToggleRequested
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
    connect(savedStreamsAction_, &QAction::triggered,
            this, &MainWindow::savedStreamsRequested);
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
    connect(videoGrid_, &VideoGridWidget::fullscreenTransitionStarted,
            this, &MainWindow::fullscreenTransitionStarted);
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

void MainWindow::installDeviceControlPanel(QWidget *panel)
{
    if (deviceControlDockWidget_ != nullptr || panel == nullptr) return;
    deviceControlDockWidget_ = new QDockWidget(tr("设备控制"), this);
    deviceControlDockWidget_->setObjectName(QStringLiteral("deviceControlDockWidget"));
    deviceControlDockWidget_->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto *scrollArea = new QScrollArea(deviceControlDockWidget_);
    scrollArea->setObjectName(QStringLiteral("deviceControlScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(panel);
    deviceControlDockWidget_->setWidget(scrollArea);
    addDockWidget(Qt::RightDockWidgetArea, deviceControlDockWidget_);
    resizeDocks({deviceControlDockWidget_}, {372}, Qt::Horizontal);
    QAction *toggle = deviceControlDockWidget_->toggleViewAction();
    toggle->setText(tr("设备控制"));
    const QList<QMenu *> menus = menuBar()->findChildren<QMenu *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!menus.isEmpty()) menus.first()->addAction(toggle);
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

void MainWindow::installEventCenterPanel(QWidget *panel)
{
    if (eventCenterDockWidget_ != nullptr || panel == nullptr) return;
    eventCenterDockWidget_ = new QDockWidget(tr("平台事件中心"), this);
    eventCenterDockWidget_->setObjectName(QStringLiteral("eventCenterDockWidget"));
    eventCenterDockWidget_->setAllowedAreas(
        Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    eventCenterDockWidget_->setWidget(panel);
    addDockWidget(Qt::BottomDockWidgetArea, eventCenterDockWidget_);
    showEventCenterAction_ = eventCenterDockWidget_->toggleViewAction();
    showEventCenterAction_->setText(tr("平台事件中心"));
    showEventCenterAction_->setObjectName(QStringLiteral("showEventCenterAction"));
    const QList<QMenu *> menus = menuBar()->findChildren<QMenu *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!menus.isEmpty()) menus.first()->addAction(showEventCenterAction_);
    eventCenterDockWidget_->hide();
}

void MainWindow::setEventCenterSummary(
    const EventCenterSummary &summary,
    bool storageWriteEnabled)
{
    if (eventCenterBadge_ == nullptr) return;
    QString severity = QStringLiteral("normal");
    QString detail;
    if (!storageWriteEnabled) {
        severity = QStringLiteral("critical");
        detail = tr("事件存储不可写");
    } else if (summary.activeCount <= 0) {
        detail = tr("事件 0");
    } else {
        switch (summary.highestSeverity) {
        case SecurityEventSeverity::Low:
            detail = tr("事件 %1 · 低").arg(summary.activeCount);
            break;
        case SecurityEventSeverity::Medium:
            severity = QStringLiteral("medium");
            detail = tr("事件 %1 · 中").arg(summary.activeCount);
            break;
        case SecurityEventSeverity::High:
            severity = QStringLiteral("high");
            detail = tr("事件 %1 · 高").arg(summary.activeCount);
            break;
        case SecurityEventSeverity::Critical:
            severity = QStringLiteral("critical");
            detail = tr("事件 %1 · 严重").arg(summary.activeCount);
            break;
        }
    }
    eventCenterBadge_->setText(detail);
    eventCenterBadge_->setProperty("severity", severity);
    eventCenterBadge_->style()->unpolish(eventCenterBadge_);
    eventCenterBadge_->style()->polish(eventCenterBadge_);
}

void MainWindow::updateAudioState(
    VideoWidget *videoWidget,
    AudioPlaybackState state,
    bool selected
)
{
    if (videoWidget == nullptr) return;
    videoWidget->setAudioPlaybackState(state, selected);
    fullscreenVideoWindow_->setAudioState(videoWidget, state, selected);
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

    emit fullscreenTransitionStarted();

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
        deviceControlVisibleBeforeMonitoringWall_ =
            deviceControlDockWidget_ != nullptr && deviceControlDockWidget_->isVisible();
        eventCenterVisibleBeforeMonitoringWall_ =
            eventCenterDockWidget_ != nullptr && eventCenterDockWidget_->isVisible();

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
        if (deviceControlDockWidget_ != nullptr) {
            deviceControlDockWidget_->hide();
        }
        if (eventCenterDockWidget_ != nullptr) {
            eventCenterDockWidget_->hide();
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
    if (deviceControlDockWidget_ != nullptr) {
        deviceControlDockWidget_->setVisible(deviceControlVisibleBeforeMonitoringWall_);
    }
    if (eventCenterDockWidget_ != nullptr) {
        eventCenterDockWidget_->setVisible(eventCenterVisibleBeforeMonitoringWall_);
    }
}

void MainWindow::setDisplayFrameRateRequest(
    const QString &requested,
    int effectiveFps
)
{
    videoGrid_->setDisplayFrameRateRequest(requested, effectiveFps);
    fullscreenVideoWindow_->setDisplayFrameRateRequest(
        requested, effectiveFps
    );
}

void MainWindow::setValidationLayoutMode(bool enabled)
{
    videoGrid_->setValidationLayoutEnabled(enabled);
    if (!enabled) {
        videoGrid_->setMonitoringWallMode(false);
        return;
    }

    // Controlled camera comparison needs a movable left-hand application
    // window next to the reference preview.  The monitoring-wall action enters
    // fullscreen and would cover that preview, so only apply its chrome/grid
    // reductions here and leave window placement to the validation controller.
    videoGrid_->setMonitoringWallMode(true);
    menuBar()->hide();
    if (videoToolBar_ != nullptr) videoToolBar_->hide();
    if (QStatusBar *existingStatusBar = findChild<QStatusBar *>(
            QString(), Qt::FindDirectChildrenOnly
        ); existingStatusBar != nullptr) {
        existingStatusBar->hide();
    }
    if (logDockWidget_ != nullptr) logDockWidget_->hide();
    if (deviceControlDockWidget_ != nullptr) deviceControlDockWidget_->hide();
    showNormal();
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

    emit fullscreenTransitionStarted();

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
    emit fullscreenTransitionStarted();
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
