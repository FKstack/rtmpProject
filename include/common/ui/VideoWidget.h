#pragma once

#include <QFrame>
#include <QImage>
#include <QPoint>
#include <QString>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QMouseEvent;
class FullscreenVideoWindow;
class VideoGridWidget;

/**
 * @brief 单路设备视频的显示槽位。
 *
 * 该类管理设备名称、状态文本和视频帧绘制，不负责拉流、解码或跨线程帧传递。
 * 播放器通过 UI 线程的信号槽更新该控件。
 *
 * @thread 仅允许在 Qt UI 线程中创建和更新。
 */
class VideoWidget final : public QFrame
{
    Q_OBJECT

public:
    /**
     * @brief 创建带有标题、状态文本和黑色视频区域的视频格。
     *
     * @param parent Qt 父对象；内部控件均以该对象或其子控件为父对象管理。
     * @thread 必须在 Qt UI 线程中调用。
     */
    explicit VideoWidget(QWidget *parent = nullptr);

    /**
     * @brief 设置界面显示的设备名称。
     *
     * 该函数只更新文本，不会建立网络连接或启动播放器。
     *
     * @param deviceName 要显示的设备名称。
     * @thread 必须在 Qt UI 线程中调用。
     */
    void setDeviceName(const QString &deviceName);

    /**
     * @brief 设置视频格当前的连接或播放状态。
     *
     * @param statusText 要显示的状态文本。
     * @thread 必须在 Qt UI 线程中调用。
     */
    void setStatusText(const QString &statusText);

    /**
     * @brief 获取当前显示的设备名称。
     *
     * @return 设备名称的值副本。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] QString deviceName() const;

    /**
     * @brief 获取当前显示的状态文本。
     *
     * @return 状态文本的值副本。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] QString statusText() const;

    /**
     * @brief 判断当前视频格是否允许发起或接收拖拽。
     *
     * 交换动画期间由 VideoGridWidget 临时关闭拖拽，防止第二次拖放干扰尚未完成的
     * 布局动画。
     *
     * @return 允许拖拽时返回 true。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] bool isDragEnabled() const noexcept;

public slots:
    /** @brief 显示一帧视频；图像数据通过 QImage 隐式共享安全持有。 */
    void displayFrame(const QImage &image);

    /** @brief 清除旧画面并恢复黑色视频区域。 */
    void clearFrame();

signals:
    /**
     * @brief 请求将源视频格与当前目标视频格交换。
     *
     * VideoWidget 只负责识别拖放目标，实际槽位交换与动画由 VideoGridWidget 统一处理。
     *
     * @param source 正在拖拽的源视频格。
     * @param target 接收拖放的目标视频格。
     * @thread 在 Qt UI 线程中发出。
     */
    void swapRequested(VideoWidget *source, VideoWidget *target);

    /**
     * @brief 请求将当前视频格切换为单路全屏预览。
     *
     * 实际全屏窗口和真实视频区域转移由上层容器统一管理，VideoWidget 不自行修改
     * 父子关系或布局。
     *
     * @param videoWidget 发起请求的视频格，即当前对象。
     * @thread 在 Qt UI 线程中发出。
     */
    void fullscreenRequested(VideoWidget *videoWidget);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum class DragState {
        Idle,
        Pressed,
        DragSource,
        DragTarget,
    };

    void startDrag();
    void setDragEnabled(bool enabled);
    void setDragState(DragState state);
    void refreshStyle();
    [[nodiscard]] QFrame *videoSurfaceForFullscreen() const noexcept;
    [[nodiscard]] bool isStatusLabelVisible() const noexcept;
    void setFullscreenSurfaceMode(bool active, bool restoreStatusLabelVisible = true);

    friend class VideoGridWidget;
    friend class FullscreenVideoWindow;

    QLabel *titleLabel_ = nullptr;
    QFrame *videoSurface_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPoint dragStartPosition_;
    DragState dragState_ = DragState::Idle;
    bool dragEnabled_ = true;
    bool mousePressed_ = false;
};
