#include "ui/MainWindow.h"

#include "ui/VideoGridWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PC 端多路 RTMP 视频显示"));
    resize(1280, 720);

    // 先固定主窗口的 UI 边界；后续播放器只需绑定网格中的单路视频格。
    setCentralWidget(new VideoGridWidget(this));
}
