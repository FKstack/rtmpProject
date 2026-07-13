#include "ui/MainWindow.h"

#include "ui/FullscreenVideoWindow.h"
#include "ui/VideoGridWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PC 端多路 RTMP 视频显示"));
    resize(1280, 720);

    // 先固定主窗口的 UI 边界；后续播放器只需绑定网格中的单路视频格。
    videoGrid_ = new VideoGridWidget(this);
    setCentralWidget(videoGrid_);

    fullscreenVideoWindow_ = new FullscreenVideoWindow(this);
    connect(videoGrid_, &VideoGridWidget::fullscreenRequested,
            this, &MainWindow::handleFullscreenRequest);
    connect(fullscreenVideoWindow_, &FullscreenVideoWindow::fullscreenExited,
            this, [this](VideoWidget *) { restoreAfterFullscreen(); });
}

MainWindow::~MainWindow()
{
    // 析构期间不应因恢复信号重新显示正在销毁的主窗口。
    wasVisibleBeforeFullscreen_ = false;
    fullscreenVideoWindow_->exitFullscreen();
}

void MainWindow::handleFullscreenRequest(VideoWidget *videoWidget)
{
    if (videoGrid_->isSwapAnimationInProgress() ||
        fullscreenVideoWindow_->isFullscreenActive()) {
        return;
    }

    wasVisibleBeforeFullscreen_ = isVisible();
    if (fullscreenVideoWindow_->enterFullscreen(videoWidget) && wasVisibleBeforeFullscreen_) {
        hide();
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
