#pragma once

#include <QMainWindow>

/**
 * @brief 应用程序的主窗口。
 *
 * 当前负责承载固定的 2x2 视频网格。后续播放器、设备管理和日志模块应通过
 * 中央控件或专用控制器接入，避免将音视频逻辑直接放入主窗口。
 *
 * @thread 仅允许在 Qt UI 线程中创建和访问。
 */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief 创建主窗口和初始视频网格。
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
    void handleFullscreenRequest(class VideoWidget *videoWidget);
    void restoreAfterFullscreen();

    class VideoGridWidget *videoGrid_ = nullptr;
    class FullscreenVideoWindow *fullscreenVideoWindow_ = nullptr;
    bool wasVisibleBeforeFullscreen_ = false;
};
