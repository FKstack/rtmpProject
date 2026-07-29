#include "ui/MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include "ui/FullscreenVideoWindow.h"
#include "logging/UserMessageService.h"
#include "media/PlaybackTypes.h"
#include "ui/LogPanel.h"
#include "ui/VideoGridWidget.h"
#include "ui/VideoWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PC 端多路 RTMP 视频显示"));
    resize(1280, 720);

    auto *videoToolBar = addToolBar(tr("视频操作"));
    videoToolBar->setObjectName(QStringLiteral("videoToolBar"));
    videoToolBar->setMovable(false);
    videoToolBar->setFloatable(false);

    addVideoAction_ = videoToolBar->addAction(tr("添加新的连接"));
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

    videoGrid_ = new VideoGridWidget(centralStack_);
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

    fullscreenVideoWindow_ = new FullscreenVideoWindow(this);

    connect(
        addVideoAction_, &QAction::triggered,
        this, &MainWindow::addConnectionRequested
    );
    connect(
        emptyAddButton_, &QPushButton::clicked,
        this, &MainWindow::addConnectionRequested
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
    connect(fullscreenVideoWindow_, &FullscreenVideoWindow::fullscreenExited,
            this, [this](VideoWidget *videoWidget) {
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
    fullscreenVideoWindow_->exitFullscreen();
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

int MainWindow::videoWidgetCount() const noexcept
{
    return videoGrid_ != nullptr ? videoGrid_->videoWidgetCount() : 0;
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

    wasVisibleBeforeFullscreen_ = isVisible();
    const bool entered = fullscreenVideoWindow_->enterFullscreen(videoWidget);
    videoGrid_->notifyFullscreenEntryResult(videoWidget, entered);

    if (entered && wasVisibleBeforeFullscreen_) {
        hide();
    } else if (!entered) {
        wasVisibleBeforeFullscreen_ = false;
    }
}

void MainWindow::restoreAfterFullscreen()
{
    if (!wasVisibleBeforeFullscreen_) {
        return;
    }

    wasVisibleBeforeFullscreen_ = false;
    show();
    raise();
    activateWindow();
}
