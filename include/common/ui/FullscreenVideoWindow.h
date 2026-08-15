#pragma once

#include <QPointer>
#include <QPointF>
#include <QString>
#include <QWidget>

#include "ui/VideoCanvasHost.h"
#include "media/PlaybackTypes.h"

class QCloseEvent;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPropertyAnimation;
class QResizeEvent;
class QScreen;
class QShortcut;
class QTimer;
class QVBoxLayout;
class QFrame;
class QLabel;
class FullscreenControlBar;
class FullscreenChromeController;
class FullscreenScreenshotService;
class VideoWidget;

/**
 * @brief 承载单路真实视频区域的独立全屏预览窗口。
 *
 * 该窗口使用独立临时画布读取当前流邮箱，不搬运 VideoWidget 或共享 OpenGL 对象。
 * 退出时由 raster 冻结层覆盖顶层窗口切换，确保网格槽位和播放器绑定不变。
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
    explicit FullscreenVideoWindow(
        RendererPreference rendererPreference = RendererPreference::Cpu,
        QWidget *parent = nullptr
    );
    void setDisplayFrameRateRequest(const QString &requested, int effectiveFps);

    /**
     * @brief 销毁前安全结束可能仍在进行的全屏退出过渡。
     *
     * @thread 必须在 Qt UI 线程中调用。
     */
    ~FullscreenVideoWindow() override;

    /**
     * @brief 将指定视频格的当前流切换到独立全屏画布。
     *
     * 源 VideoWidget 继续停留在原 QGridLayout 槽位中；全屏画布只注册同一帧邮箱。
     *
     * @param videoWidget 要全屏预览的视频格。
     * @return 成功进入全屏时返回 true；窗口已激活或源控件状态无效时返回 false。
     * @thread 必须在 Qt UI 线程中调用。
     */
    bool enterFullscreen(VideoWidget *videoWidget);

    /**
     * @brief 启动 raster 冻结过渡并请求恢复主窗口画布。
     *
     * 无活动视频时该函数无副作用。
     *
     * @thread 必须在 Qt UI 线程中调用。
     */
    void exitFullscreen();

    /**
     * @brief 完成退出过渡并真正隐藏全屏顶层窗口。
     *
     * MainWindow 在主画布完成首次呈现后调用；若未调用，内部 750ms
     * 安全计时器会自动收尾。
     */
    void completeExitTransition();

    /** @brief 设置截图保存目录；空字符串恢复平台默认图片目录。 */
    void setScreenshotOutputDirectory(QString directory);

    /**
     * @brief 判断窗口是否处于进入、显示或退出全屏状态。
     *
     * @return 正在全屏预览时返回 true。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] bool isFullscreenActive() const noexcept;
    [[nodiscard]] RenderRuntimeMetrics rendererRuntimeMetrics() const;
    void setAudioState(
        VideoWidget *videoWidget,
        AudioPlaybackState state,
        bool selected
    );

signals:
    /**
     * @brief 临时全屏画布完成绑定并显示后发出。
     *
     * @param videoWidget 当前全屏的视频格。
     * @thread 在 Qt UI 线程中发出。
     */
    void fullscreenEntered(VideoWidget *videoWidget);

    /**
     * @brief 全屏窗口开始 raster 退出过渡时发出。
     *
     * 该信号用于让动态网格在主画布恢复期间禁止添加和拖拽，避免布局事务与
     * 全屏过渡并发执行。
     *
     * @param videoWidget 正在退出全屏的视频格。
     * @thread 在 Qt UI 线程中发出。
     */
    void fullscreenExitStarted(VideoWidget *videoWidget);

    /**
     * @brief 请求 MainWindow 在当前 raster 过渡图后方恢复主画布。
     *
     * fullscreenExited 只会在主画布呈现或安全超时后发出。
     */
    void fullscreenRestoreRequested(VideoWidget *videoWidget);

    /**
     * @brief raster 过渡窗口真正隐藏后发出。
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

    /** @brief PNG 已原子保存。 */
    void screenshotSaved(const QString &path);

    /** @brief 截图失败；reason 不含流 URL 等敏感信息。 */
    void screenshotFailed(const QString &reason);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    };

    static constexpr int kFirstFrameHideDelayMs = 1200;
    static constexpr int kPointerLeaveHideDelayMs = 250;
    static constexpr int kExitTransitionTimeoutMs = 750;
    static constexpr int kScreenshotToastDurationMs = 2500;

    [[nodiscard]] QScreen *screenForVideoWidget(const VideoWidget *videoWidget) const;
    void handlePointerActivity(const QPointF &position);
    void showControlBar(bool animated);
    void scheduleControlBarHide(int delayMs = kPointerLeaveHideDelayMs);
    void positionControlBar();
    void stopControlBarMotion();
    void updateRevealZoneGeometry();
    void refreshActivePresentation();
    void synchronizeCanvasPresentation();
    void scheduleCanvasPresentationSync(quint64 generation);
    void refreshRenderSnapshot();
    void requestScreenshot();
    void showScreenshotToast(const QImage &thumbnail, const QString &message,
                             bool autoHide);
    void positionScreenshotToast();
    void beginExitTransition(VideoWidget *videoWidget);
    void updateTransitionOverlayGeometry();
    void clearRestoreState();
    [[nodiscard]] const char *transitionStateName() const noexcept;

    QVBoxLayout *videoLayout_ = nullptr;
    VideoCanvasHost *canvasHost_ = nullptr;
    FullscreenControlBar *controlBar_ = nullptr;
    QWidget *revealZone_ = nullptr;
    FullscreenChromeController *chromeController_ = nullptr;
    FullscreenScreenshotService *screenshotService_ = nullptr;
    QShortcut *screenshotShortcut_ = nullptr;
    QFrame *screenshotToast_ = nullptr;
    QLabel *screenshotThumbnailLabel_ = nullptr;
    QLabel *screenshotMessageLabel_ = nullptr;
    QTimer *screenshotToastTimer_ = nullptr;
    QLabel *transitionOverlay_ = nullptr;
    QTimer *exitTransitionTimer_ = nullptr;
    QMetaObject::Connection renderStateConnection_;
    VideoSurfaceRestoreState restoreState_;
    QPointer<VideoWidget> exitingVideoWidget_;
    QString screenshotOutputDirectory_;
    QString latestScreenshotPath_;
    bool lastFrameVisible_ = false;
    quint64 presentationGeneration_ = 0;
    TransitionState transitionState_ = TransitionState::Windowed;
};
