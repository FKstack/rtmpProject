#pragma once

#include <QMainWindow>

class QAction;
class FullscreenVideoWindow;
class VideoGridWidget;
class VideoWidget;

/**
 * @brief 应用程序的主窗口。
 *
 * 当前负责承载动态视频网格、添加视频窗口工具栏，并协调普通网格与单路全屏窗口。
 * 音视频拉流和解码逻辑仍应由后续专用控制器管理。
 *
 * @thread 仅允许在 Qt UI 线程中创建和访问。
 */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief 创建主窗口、动态视频网格和添加操作。
     *
     * @param parent Qt 父对象；非空时由父对象管理窗口生命周期。
     * @thread 必须在 Qt UI 线程中调用。
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief 析构前恢复可能处于全屏窗口中的真实视频区域。
     *
     * @thread 必须在 Qt UI 线程中调用。
     */
    ~MainWindow() override;

private:
    void addVideoWidget();
    void updateAddVideoAction();
    void handleFullscreenRequest(VideoWidget *videoWidget);
    void restoreAfterFullscreen();

    VideoGridWidget *videoGrid_ = nullptr;
    FullscreenVideoWindow *fullscreenVideoWindow_ = nullptr;
    QAction *addVideoAction_ = nullptr;
    bool wasVisibleBeforeFullscreen_ = false;
};
