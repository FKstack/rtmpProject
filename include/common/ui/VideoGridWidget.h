#pragma once

#include <QPointer>
#include <QMargins>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

#include "ui/VideoCanvasHost.h"

class QLabel;
class QEvent;
class QGridLayout;
class QKeyEvent;
class QMouseEvent;
class QParallelAnimationGroup;
class QPixmap;
class QResizeEvent;
class QShowEvent;
class VideoWidget;

/**
 * @brief 动态视频网格的行列数量。
 */
struct GridDimensions
{
    int rows {};
    int columns {};
};

/** @brief 16:9 监控网格在当前可用区域中的纯几何计算结果。 */
struct MonitoringGridGeometry
{
    QRect gridRect;
    QSize cellSize;
    QSize videoViewportSize;
    QMargins layoutMargins;

    [[nodiscard]] bool isValid() const noexcept
    {
        return !gridRect.isEmpty() && cellSize.isValid() &&
               videoViewportSize.isValid();
    }
};

/**
 * @brief 管理 0～16 个视频格的动态网格容器。
 *
 * 该类拥有全部 VideoWidget，并以 videoWidgets_ 的顺序作为设备与视觉槽位的唯一
 * 映射。添加、交换和全屏切换通过统一状态互斥，避免多个布局事务并发修改控件树。
 *
 * @thread 仅允许在 Qt UI 线程中创建和访问。
 */
class VideoGridWidget final : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 网格当前允许执行的交互状态。
     */
    enum class GridInteractionState {
        Idle,
        AddingWidget,
        SwappingWidgets,
        EnteringFullscreen,
        Fullscreen,
        ExitingFullscreen,
    };
    Q_ENUM(GridInteractionState)

    /** @brief 网格允许同时管理的视频格上限。 */
    static constexpr int kMaximumVideoWidgetCount = 16;

    /**
     * @brief 创建初始为空的动态网格。
     *
     * @param parent Qt 父对象；创建的视频格由该网格及其布局管理。
     * @thread 必须在 Qt UI 线程中调用。
     */
    explicit VideoGridWidget(
        RendererPreference rendererPreference = RendererPreference::Cpu,
        QWidget *parent = nullptr
    );

    /**
     * @brief 计算指定视频数量对应的行列。
     *
     * 结果在 1～16 范围内尽量接近正方形，并优先减少空白槽位。输入不在有效
     * 范围时返回 {0, 0}。
     *
     * @param widgetCount 要排列的视频格数量。
     * @return 对应的网格行列。
     */
    [[nodiscard]] static GridDimensions calculateGridDimensions(int widgetCount) noexcept;

    /**
     * @brief 计算保持统一视频比例、整体居中的最大监控网格。
     */
    [[nodiscard]] static MonitoringGridGeometry calculateMonitoringGridGeometry(
        QSize availableSize,
        GridDimensions dimensions,
        QSize videoChromeSize,
        QMargins baseMargins = QMargins(4, 4, 4, 4),
        int spacing = 4,
        QSize videoAspect = QSize(16, 9)
    ) noexcept;

    /**
     * @brief 获取当前网格实际使用的行列。
     *
     * @return 当前视频数量对应的网格行列。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] GridDimensions gridDimensions() const noexcept;

    /**
     * @brief 获取当前网格中的视频格数量。
     *
     * @return 已真实创建并由网格管理的视频格数量。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] int videoWidgetCount() const noexcept;

    /**
     * @brief 获取指定逻辑索引对应的视频显示槽位。
     *
     * @param index 从 0 开始的逻辑及视觉索引。
     * @return 对应的 VideoWidget；索引越界时返回 nullptr。
     * @note 返回的指针由 VideoGridWidget 管理，调用方不得释放。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] VideoWidget *videoWidgetAt(int index) const noexcept;

    /**
     * @brief 判断当前是否允许添加新的视频格。
     *
     * 只有空闲状态且当前数量小于 16 时允许添加。
     *
     * @return 可以立即添加时返回 true。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] bool canAddVideoWidget() const noexcept;

    /**
     * @brief 获取网格当前交互状态。
     *
     * @return 当前添加、交换或全屏协调状态。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] GridInteractionState interactionState() const noexcept;
    [[nodiscard]] MonitoringGridGeometry monitoringGridGeometry() const noexcept;
    [[nodiscard]] bool isMonitoringWallMode() const noexcept;
    void setMonitoringWallMode(bool enabled);

    /**
     * @brief 创建并添加一个新的视频格。
     *
     * 新控件会获得唯一的 Camera 编号、完整的拖拽与全屏信号连接，并通过快照
     * 动画加入动态布局。动画或全屏期间的重复请求会被忽略。
     *
     * @return 成功创建的视频格；达到上限或当前不可添加时返回 nullptr。
     * @note 返回的指针由 VideoGridWidget 管理，调用方不得释放。
     * @thread 必须在 Qt UI 线程中调用。
     */
    VideoWidget *addVideoWidget();
    VideoWidget *addVideoWidget(const QString &deviceName);

    /**
     * @brief 移除并延迟销毁一个不在全屏状态的视频格。
     */
    bool removeVideoWidget(VideoWidget *videoWidget);

    /**
     * @brief 判断是否正在执行视频格交换动画。
     *
     * @return 当前状态为 SwappingWidgets 时返回 true。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] bool isSwapAnimationInProgress() const noexcept;

    /**
     * @brief 交换两个逻辑槽位中的实际 VideoWidget 对象。
     *
     * 该方法先更新逻辑顺序和动态布局，再使用两个控件快照执行双向位移动画。
     *
     * @param firstIndex 第一个从 0 开始的槽位索引。
     * @param secondIndex 第二个从 0 开始的槽位索引。
     * @return 成功启动交换动画时返回 true；索引无效或网格非空闲时返回 false。
     * @thread 必须在 Qt UI 线程中调用。
     */
    bool swapVideoWidgets(int firstIndex, int secondIndex);

    /**
     * @brief 接收 MainWindow 对全屏进入请求的处理结果。
     *
     * @param videoWidget 此次请求对应的视频格。
     * @param entered FullscreenVideoWindow 是否成功进入全屏。
     * @thread 必须在 Qt UI 线程中调用。
     */
    void notifyFullscreenEntryResult(VideoWidget *videoWidget, bool entered);

    /**
     * @brief 通知网格全屏窗口已经开始退出。
     *
     * @param videoWidget 正在恢复的视频格。
     * @thread 必须在 Qt UI 线程中调用。
     */
    void notifyFullscreenExitStarted(VideoWidget *videoWidget);

    /**
     * @brief 通知网格全屏视频区域已经完成恢复。
     *
     * @param videoWidget 已恢复的视频格；源控件提前销毁时可以为 nullptr。
     * @thread 必须在 Qt UI 线程中调用。
     */
    void notifyFullscreenExited(VideoWidget *videoWidget);

    /**
     * @brief 进入画布内全屏（EGLFS 单窗口平台）：主画布切换为单路 Snapshot。
     *
     * 不创建第二个 QOpenGLWidget 或顶层窗口；必须由 MainWindow 在
     * fullscreenRequested 握手（EnteringFullscreen 状态）期间调用，并通过
     * notifyFullscreenEntryResult() 回传结果。
     *
     * @return 成功切换主画布 Snapshot 时返回 true。
     */
    bool enterInCanvasFullscreen(VideoWidget *videoWidget);

    /**
     * @brief 退出画布内全屏：恢复网格 Snapshot 与 15 FPS 调度。
     */
    void exitInCanvasFullscreen();

    /** @brief 当前是否处于画布内全屏。 */
    [[nodiscard]] bool isInCanvasFullscreenActive() const noexcept;
    void bindVideoStream(
        VideoWidget *videoWidget,
        StreamId streamId,
        std::shared_ptr<LatestFrameMailbox> mailbox
    );
    void unbindVideoStream(VideoWidget *videoWidget);
    void refreshRenderSnapshot();
    [[nodiscard]] QString activeRendererBackend() const;
    [[nodiscard]] RenderStatistics renderStatistics() const noexcept;
    [[nodiscard]] RenderRuntimeMetrics rendererRuntimeMetrics() const;

protected:
    void changeEvent(QEvent *event) override;
    QSize minimumSizeHint() const override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

signals:
    /** @brief 视频格逻辑数量发生变化。 */
    void videoWidgetCountChanged(int count);

    /** @brief 新视频格的添加动画完成，控件已经恢复交互。 */
    void videoWidgetAdded(VideoWidget *videoWidget);

    /** @brief 视频格已经从网格解除并等待销毁。 */
    void videoWidgetRemoved(VideoWidget *videoWidget);

    /** @brief 当前视频格数量已经达到上限。 */
    void maximumVideoWidgetCountReached();

    /** @brief 添加、交换或全屏协调状态发生变化。 */
    void gridInteractionStateChanged(VideoGridWidget::GridInteractionState state);

    /**
     * @brief 在两个视频格的交换动画完成后发出。
     *
     * @param firstIndex 交换前的第一个槽位索引。
     * @param secondIndex 交换前的第二个槽位索引。
     */
    void videoWidgetsSwapped(int firstIndex, int secondIndex);

    /**
     * @brief 转发某个视频格的全屏预览请求。
     *
     * 发出前网格已经进入 EnteringFullscreen，MainWindow 必须通过
     * notifyFullscreenEntryResult() 回传结果。
     *
     * @param videoWidget 请求全屏的视频格。
     */
    void fullscreenRequested(VideoWidget *videoWidget);

    /** @brief 主 CPU/OpenGL 画布完成一次真实呈现。 */
    void surfacePresented();

private:
    static constexpr int kMaximumGridDimension = 4;

    VideoWidget *createVideoWidget(const QString &deviceName);
    void connectVideoWidgetSignals(VideoWidget *videoWidget);
    void handleRenderStateChanged(VideoWidget *videoWidget);
    void handleSwapRequested(VideoWidget *source, VideoWidget *target);
    void handleFullscreenRequested(VideoWidget *videoWidget);
    [[nodiscard]] int indexOf(const VideoWidget *videoWidget) const noexcept;
    void relayoutVideoWidgets();
    void updateMonitoringGridGeometry();
    [[nodiscard]] QSize maximumVideoChromeSizeHint() const;
    void setInteractionState(GridInteractionState state);
    void setDragEnabledForAll(bool enabled);
    void applyInCanvasFullscreenSnapshot();
    [[nodiscard]] QLabel *createSnapshotOverlay(const QPixmap &pixmap,
                                                const QRect &geometry);
    [[nodiscard]] QPixmap captureWidgetSnapshot(VideoWidget *videoWidget);

    QVector<VideoWidget *> videoWidgets_;
    VideoCanvasHost *canvasHost_ = nullptr;
    QGridLayout *gridLayout_ = nullptr;
    QPointer<QParallelAnimationGroup> interactionAnimation_;
    QPointer<VideoWidget> fullscreenVideoWidget_;
    GridInteractionState interactionState_ = GridInteractionState::Idle;
    MonitoringGridGeometry monitoringGridGeometry_;
    bool monitoringWallMode_ = false;
    bool inCanvasFullscreen_ = false;
    int nextCameraNumber_ = 1;
};
