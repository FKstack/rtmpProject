#include "ui/MainWindow.h"

#include <QAction>
#include <QStatusBar>
#include <QToolBar>

#include "ui/FullscreenVideoWindow.h"
#include "ui/VideoGridWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PC 端多路 RTMP 视频显示"));
    resize(1280, 720);

    auto *videoToolBar = addToolBar(tr("视频操作"));
    videoToolBar->setObjectName(QStringLiteral("videoToolBar"));
    videoToolBar->setMovable(false);
    videoToolBar->setFloatable(false);

    addVideoAction_ = videoToolBar->addAction(tr("添加视频窗口"));
    addVideoAction_->setObjectName(QStringLiteral("addVideoWidgetAction"));

    videoGrid_ = new VideoGridWidget(this);
    setCentralWidget(videoGrid_);

    fullscreenVideoWindow_ = new FullscreenVideoWindow(this);

    connect(addVideoAction_, &QAction::triggered, this, &MainWindow::addVideoWidget);
    connect(videoGrid_, &VideoGridWidget::videoWidgetCountChanged,
            this, [this](int) { updateAddVideoAction(); });
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
}

MainWindow::~MainWindow()
{
    // 析构期间不应因恢复信号重新显示正在销毁的主窗口。
    wasVisibleBeforeFullscreen_ = false;
    fullscreenVideoWindow_->exitFullscreen();
}

void MainWindow::addVideoWidget()
{
    videoGrid_->addVideoWidget();
    updateAddVideoAction();
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
