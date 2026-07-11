#include "ui/MainWindow.h"

#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PC 端多路 RTMP 视频显示"));
    resize(1280, 720);
    setCentralWidget(new QWidget(this));
}
