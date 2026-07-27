#pragma once

#include <QMainWindow>

class QAction;
class FullscreenVideoWindow;
class QPushButton;
class QStackedWidget;
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

    /** @brief 为一个已经校验的连接创建显示格。 */
    VideoWidget *addConnectionWidget(const QString &displayName);

    /** @brief 从布局移除一个连接显示格。 */
    bool removeConnectionWidget(VideoWidget *videoWidget);

    [[nodiscard]] int videoWidgetCount() const noexcept;

signals:
    /** @brief 中央按钮或工具栏请求打开连接对话框。 */
    void addConnectionRequested();

private:
    void updateAddVideoAction();
    void updateCentralPage();
    void handleFullscreenRequest(VideoWidget *videoWidget);
    void restoreAfterFullscreen();

    VideoGridWidget *videoGrid_ = nullptr;
    FullscreenVideoWindow *fullscreenVideoWindow_ = nullptr;
    QAction *addVideoAction_ = nullptr;
    QStackedWidget *centralStack_ = nullptr;
    QWidget *emptyPage_ = nullptr;
    QPushButton *emptyAddButton_ = nullptr;
    bool wasVisibleBeforeFullscreen_ = false;
};
