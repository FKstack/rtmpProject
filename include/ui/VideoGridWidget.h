#pragma once

#include <array>

#include <QWidget>

class QLabel;
class QGridLayout;
class QParallelAnimationGroup;
class VideoWidget;

/**
 * @brief 固定 2x2 布局的多路视频容器。
 *
 * 当前版本创建四个 VideoWidget，分别对应 camera001 到 camera004。该类拥有
 * 所有视频格，调用方只能借用 videoWidgetAt() 返回的指针，不能释放它们。
 *
 * @thread 仅允许在 Qt UI 线程中创建和访问。
 */
class VideoGridWidget final : public QWidget
{
    Q_OBJECT

public:
    /** @brief 当前固定布局中的视频格数量。 */
    static constexpr int kVideoWidgetCount = 4;

    /**
     * @brief 创建固定的 2x2 视频网格。
     *
     * @param parent Qt 父对象；创建的视频格由该网格及其布局管理。
     * @thread 必须在 Qt UI 线程中调用。
     */
    explicit VideoGridWidget(QWidget *parent = nullptr);

    /**
     * @brief 获取当前网格中的视频格数量。
     *
     * @return 固定布局中的视频格数量。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] int videoWidgetCount() const noexcept;

    /**
     * @brief 获取指定索引对应的视频显示槽位。
     *
     * @param index 从 0 开始的网格索引。
     * @return 对应的 VideoWidget；索引越界时返回 nullptr。
     * @note 返回的指针由 VideoGridWidget 管理，调用方不得释放。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] VideoWidget *videoWidgetAt(int index) const noexcept;

    /**
     * @brief 判断是否正在执行视频格交换动画。
     *
     * 全屏预览在交换动画完成前不得转移视频区域，否则布局快照与真实控件状态会冲突。
     *
     * @return 正在交换时返回 true。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] bool isSwapAnimationInProgress() const noexcept;

    /**
     * @brief 交换两个固定槽位中的实际 VideoWidget 对象。
     *
     * 该方法先更新布局与槽位映射，再使用两个控件快照执行双向位移动画。因此设备名称、
     * 状态、视频区域和后续播放器绑定会作为同一对象整体移动。
     *
     * @param firstIndex 第一个从 0 开始的槽位索引。
     * @param secondIndex 第二个从 0 开始的槽位索引。
     * @return 成功启动交换动画时返回 true；索引无效、索引相同或已有动画时返回 false。
     * @thread 必须在 Qt UI 线程中调用。
     */
    bool swapVideoWidgets(int firstIndex, int secondIndex);

signals:
    /**
     * @brief 在两个视频格的交换动画完成后发出。
     *
     * @param firstIndex 交换前的第一个槽位索引。
     * @param secondIndex 交换前的第二个槽位索引。
     * @thread 在 Qt UI 线程中发出。
     */
    void videoWidgetsSwapped(int firstIndex, int secondIndex);

    /**
     * @brief 转发某个视频格的全屏预览请求。
     *
     * @param videoWidget 请求全屏的视频格。
     * @thread 在 Qt UI 线程中发出。
     */
    void fullscreenRequested(VideoWidget *videoWidget);

private:
    static constexpr int kColumnCount = 2;

    void handleSwapRequested(VideoWidget *source, VideoWidget *target);
    [[nodiscard]] int indexOf(const VideoWidget *videoWidget) const noexcept;
    void setDragEnabledForAll(bool enabled);
    [[nodiscard]] QLabel *createSnapshotOverlay(VideoWidget *videoWidget);

    std::array<VideoWidget *, kVideoWidgetCount> videoWidgets_{};
    QGridLayout *gridLayout_ = nullptr;
    QParallelAnimationGroup *swapAnimation_ = nullptr;
    bool swapAnimationInProgress_ = false;
};
