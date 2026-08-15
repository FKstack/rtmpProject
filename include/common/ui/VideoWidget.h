#pragma once

#include <QFrame>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <memory>

#include "media/LatestFrameMailbox.h"
#include "media/PlaybackTypes.h"
#include "render/RenderTypes.h"
#include "device_control/DeviceControlTypes.h"

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QContextMenuEvent;
class QEvent;
class QLabel;
class QToolButton;
class QMouseEvent;
class QResizeEvent;
class QVBoxLayout;
class VideoGridWidget;

/**
 * @brief 单路设备视频的显示槽位。
 *
 * 该类管理设备名称、状态文本、显示模式和交互，并作为共享画布的视频区域几何
 * 锚点；实际像素由 VideoCanvasHost 统一合成，不由该控件保存或绘制。
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
    void bindRenderSource(
        StreamId streamId,
        std::shared_ptr<LatestFrameMailbox> mailbox
    );
    void unbindRenderSource();
    [[nodiscard]] StreamId streamId() const noexcept;
    [[nodiscard]] std::shared_ptr<LatestFrameMailbox> frameMailbox() const;
    [[nodiscard]] QRect videoViewportRect(const QWidget *ancestor) const;
    /** @brief 返回标题、布局间距和内边距占用的非视频尺寸。 */
    [[nodiscard]] QSize videoChromeSizeHint() const;
    [[nodiscard]] bool isFrameVisible() const noexcept;
    /** @brief 返回该视频格在主网格中的等比显示策略。 */
    [[nodiscard]] VideoDisplayMode displayMode() const noexcept;
    /**
     * @brief 切换该视频格的等比显示策略并请求共享画布刷新。
     *
     * Cover 铺满视频区域但可能居中裁剪；Contain 保留完整画面但可能出现黑边。
     */
    void setDisplayMode(VideoDisplayMode mode);
    void setTitleOverlayEnabled(bool enabled);
    void setAudioPlaybackState(
        AudioPlaybackState state,
        bool selected
    );
    [[nodiscard]] AudioPlaybackState audioPlaybackState() const noexcept;
    [[nodiscard]] bool isAudioSelected() const noexcept;
    void setDevicePresenceState(DevicePresenceState state);
    [[nodiscard]] DevicePresenceState devicePresenceState() const noexcept;
    void setControlTargetSelected(bool selected);
    [[nodiscard]] bool isControlTargetSelected() const noexcept;

public slots:
    /** @brief 标记该流已有可显示帧；实际像素由共享画布从邮箱读取。 */
    void showFrame();

    /** @brief 清除旧画面并恢复黑色视频区域。 */
    void clearFrame();

signals:
    void renderStateChanged(VideoWidget *videoWidget);
    /** @brief 用户从右键菜单请求重连当前稳定连接。 */
    void reconnectRequested(VideoWidget *videoWidget);

    /** @brief 用户从右键菜单请求断开并移除当前连接。 */
    void removeRequested(VideoWidget *videoWidget);

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
    void audioToggleRequested(VideoWidget *videoWidget);
    void controlTargetRequested(VideoWidget *videoWidget);

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

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
    void updateMonitoringMinimumSize();
    void updateTitleOverlay();
    friend class VideoGridWidget;

    QLabel *titleLabel_ = nullptr;
    QLabel *presenceBadge_ = nullptr;
    QToolButton *audioButton_ = nullptr;
    QFrame *videoSurface_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QVBoxLayout *rootLayout_ = nullptr;
    QString deviceName_;
    QPoint dragStartPosition_;
    DragState dragState_ = DragState::Idle;
    bool dragEnabled_ = true;
    bool mousePressed_ = false;
    bool frameVisible_ = false;
    bool titleOverlayEnabled_ = true;
    AudioPlaybackState audioState_ = AudioPlaybackState::Unavailable;
    bool audioSelected_ = false;
    DevicePresenceState presenceState_ = DevicePresenceState::Unavailable;
    bool controlTargetSelected_ = false;
    VideoDisplayMode displayMode_ = VideoDisplayMode::Contain;
    StreamId streamId_ = kInvalidStreamId;
    std::shared_ptr<LatestFrameMailbox> frameMailbox_;
};
