#pragma once

#include <array>

#include <QWidget>

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

private:
    std::array<VideoWidget *, kVideoWidgetCount> videoWidgets_{};
};
