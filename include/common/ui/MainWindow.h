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
 * 音视频拉流和解码由应用组合层持有的播放器管理。
 *
 * @thread 仅允许在 Qt UI 线程中创建和访问。
 */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    /** @brief 应用启动时确定性创建的真实播放格数量。 */
    static constexpr int kInitialPlaybackWidgetCount = 4;

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

    /**
     * @brief 返回当前网格中指定逻辑索引的视频格。
     *
     * @param index 从 0 开始的当前网格逻辑索引。
     * @return 对应 VideoWidget；索引越界时返回 nullptr。
     * @note 返回指针由 MainWindow 内部网格管理，调用方不得释放。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] VideoWidget *videoWidgetAt(int index) const noexcept;

    /** @brief 返回当前网格中的 Camera 01，供应用组合层绑定一路播放器。 */
    [[nodiscard]] VideoWidget *primaryVideoWidget() const noexcept;

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
