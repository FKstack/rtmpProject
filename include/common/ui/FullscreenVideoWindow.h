#pragma once

#include <QPointer>
#include <QPointF>
#include <QSizePolicy>
#include <QWidget>

class QCloseEvent;
class QFrame;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QScreen;
class QTimer;
class QVBoxLayout;
class FullscreenControlBar;
class VideoWidget;

/**
 * @brief 承载单路真实视频区域的独立全屏预览窗口。
 *
 * 该窗口只临时转移 VideoWidget 内的 videoSurface，而不复制或创建第二个渲染目标。
 * 退出时按保存的父对象和布局信息恢复原视频区域，确保网格槽位和未来播放器绑定不变。
 *
 * @thread 仅允许在 Qt UI 线程中创建和访问。
 */
class FullscreenVideoWindow final : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 创建全屏视频窗口及其覆盖式控制栏。
     *
     * @param parent Qt 所有者；通常为 MainWindow，窗口仍以独立顶层窗口方式显示。
     * @thread 必须在 Qt UI 线程中调用。
     */
    explicit FullscreenVideoWindow(QWidget *parent = nullptr);

    /**
     * @brief 销毁前恢复仍在全屏窗口中的真实视频区域。
     *
     * @thread 必须在 Qt UI 线程中调用。
     */
    ~FullscreenVideoWindow() override;

    /**
     * @brief 将指定视频格的真实视频区域切换到全屏窗口。
     *
     * 该操作不复制视频控件；源 VideoWidget 继续停留在原 QGridLayout 槽位中。
     *
     * @param videoWidget 要全屏预览的视频格。
     * @return 成功进入全屏时返回 true；窗口已激活或源控件状态无效时返回 false。
     * @thread 必须在 Qt UI 线程中调用。
     */
    bool enterFullscreen(VideoWidget *videoWidget);

    /**
     * @brief 退出全屏并恢复原视频区域的父子关系、布局和可见状态。
     *
     * 无活动视频时该函数无副作用。
     *
     * @thread 必须在 Qt UI 线程中调用。
     */
    void exitFullscreen();

    /**
     * @brief 判断窗口当前是否承载某个视频格的真实视频区域。
     *
     * @return 正在全屏预览时返回 true。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] bool isFullscreenActive() const noexcept;

signals:
    /**
     * @brief 真实视频区域完成转移并显示全屏窗口后发出。
     *
     * @param videoWidget 当前全屏的视频格。
     * @thread 在 Qt UI 线程中发出。
     */
    void fullscreenEntered(VideoWidget *videoWidget);

    /**
     * @brief 真实视频区域开始从全屏窗口恢复到原视频格时发出。
     *
     * 该信号用于让动态网格在父子关系恢复期间禁止添加和拖拽，避免布局事务与
     * 全屏恢复并发执行。
     *
     * @param videoWidget 正在退出全屏的视频格。
     * @thread 在 Qt UI 线程中发出。
     */
    void fullscreenExitStarted(VideoWidget *videoWidget);

    /**
     * @brief 真实视频区域恢复到原视频格后发出。
     *
     * @param videoWidget 已恢复的视频格；恢复目标失效时可能为 nullptr。
     * @thread 在 Qt UI 线程中发出。
     */
    void fullscreenExited(VideoWidget *videoWidget);

    /**
     * @brief 转发控制栏的静音请求，供后续播放器控制器订阅。
     *
     * @param videoWidget 当前全屏的视频格。
     * @thread 在 Qt UI 线程中发出。
     */
    void muteRequested(VideoWidget *videoWidget);

    /**
     * @brief 转发控制栏的截图请求，供后续播放器控制器订阅。
     *
     * @param videoWidget 当前全屏的视频格。
     * @thread 在 Qt UI 线程中发出。
     */
    void screenshotRequested(VideoWidget *videoWidget);

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum class TransitionState {
        Windowed,
        Entering,
        Fullscreen,
        Exiting,
    };

    struct VideoSurfaceRestoreState {
        QPointer<VideoWidget> videoWidget;
        QPointer<QWidget> originalParent;
        QPointer<QVBoxLayout> originalLayout;
        int layoutIndex = -1;
        int layoutStretch = 0;
        QSizePolicy sizePolicy;
        bool surfaceWasVisible = false;
        bool statusLabelWasVisible = false;
    };

    static constexpr int kControlBarBottomMargin = 20;
    static constexpr int kControlBarRevealHeight = 120;
    static constexpr int kControlBarAutoHideDelayMs = 2500;

    [[nodiscard]] QScreen *screenForVideoWidget(const VideoWidget *videoWidget) const;
    [[nodiscard]] bool isPointerInRevealArea(const QPointF &position) const noexcept;
    void showControlBar(bool restartTimer);
    void scheduleControlBarHide();
    void hideControlBar();
    void positionControlBar();
    void clearRestoreState();
    void logFullscreenEvent(const QObject *watched, int eventType) const;
    [[nodiscard]] const char *transitionStateName() const noexcept;

    QVBoxLayout *videoLayout_ = nullptr;
    FullscreenControlBar *controlBar_ = nullptr;
    QTimer *autoHideTimer_ = nullptr;
    QFrame *activeVideoSurface_ = nullptr;
    VideoSurfaceRestoreState restoreState_;
    TransitionState transitionState_ = TransitionState::Windowed;
};
