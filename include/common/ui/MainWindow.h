#pragma once

#include <QByteArray>
#include <QMainWindow>

#include <memory>
#include <cstdint>

#include "logging/UserMessageTypes.h"
#include "ui/FullscreenPresentationMode.h"
#include "media/PlaybackTypes.h"

class QAction;
class QDockWidget;
class QEvent;
class QKeyEvent;
class QToolBar;
class QToolButton;
class FullscreenVideoWindow;
class LogPanel;
class UserMessageService;
class QPushButton;
class QStackedWidget;
class VideoGridWidget;
class VideoWidget;
enum class DeviceStatus;
enum class RendererPreference;
class LatestFrameMailbox;
struct RenderRuntimeMetrics;
struct EventCenterSummary;
using StreamId = std::uint64_t;

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
    explicit MainWindow(
        RendererPreference rendererPreference,
        QWidget *parent = nullptr
    );
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
    void bindVideoStream(
        VideoWidget *videoWidget,
        StreamId streamId,
        std::shared_ptr<LatestFrameMailbox> mailbox
    );

    [[nodiscard]] int videoWidgetCount() const noexcept;
    [[nodiscard]] RenderRuntimeMetrics rendererRuntimeMetrics() const;
    void setDisplayFrameRateRequest(const QString &requested, int effectiveFps);
    void setValidationLayoutMode(bool enabled);
    void installDeviceControlPanel(QWidget *panel);
    void installEventCenterPanel(QWidget *panel);
    void setEventCenterSummary(const EventCenterSummary &summary,
                               bool storageWriteEnabled);

    /** @brief 连接普通运行消息源并显示到底部运行消息面板。 */
    void setUserMessageService(UserMessageService *service);

    /** @brief 更新指定视频格的设备状态和可选错误详情。 */
    void updateDeviceStatus(
        VideoWidget *videoWidget,
        DeviceStatus status,
        UserFailureReason reason = UserFailureReason::None
    );
    void updateAudioState(
        VideoWidget *videoWidget,
        AudioPlaybackState state,
        bool selected
    );

    [[nodiscard]] QDockWidget *logDockWidget() const noexcept;
    [[nodiscard]] LogPanel *logPanel() const noexcept;
    [[nodiscard]] bool isMonitoringWallMode() const noexcept;

public slots:
    void setMonitoringWallMode(bool enabled);

signals:
    /** @brief 中央按钮或工具栏请求打开连接对话框。 */
    void addConnectionRequested();
    void savedStreamsRequested();
    /** @brief 任一全屏模式开始切换，供上层触发设备安全停车。 */
    void fullscreenTransitionStarted();
    void audioToggleRequested(VideoWidget *videoWidget);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateAddVideoAction();
    void updateCentralPage();
    void handleFullscreenRequest(VideoWidget *videoWidget);
    void beginRestoreBehindFullscreen(VideoWidget *videoWidget);
    void restoreAfterFullscreen();

    VideoGridWidget *videoGrid_ = nullptr;
    FullscreenVideoWindow *fullscreenVideoWindow_ = nullptr;
    QAction *addVideoAction_ = nullptr;
    QAction *savedStreamsAction_ = nullptr;
    QStackedWidget *centralStack_ = nullptr;
    QWidget *emptyPage_ = nullptr;
    QPushButton *emptyAddButton_ = nullptr;
    QDockWidget *logDockWidget_ = nullptr;
    QDockWidget *deviceControlDockWidget_ = nullptr;
    QDockWidget *eventCenterDockWidget_ = nullptr;
    LogPanel *logPanel_ = nullptr;
    QToolBar *videoToolBar_ = nullptr;
    QAction *showLogAction_ = nullptr;
    QAction *showEventCenterAction_ = nullptr;
    QToolButton *eventCenterBadge_ = nullptr;
    QAction *monitoringWallAction_ = nullptr;
    QMetaObject::Connection logConnection_;
    QMetaObject::Connection fullscreenRestorePaintConnection_;
    QByteArray geometryBeforeMonitoringWall_;
    Qt::WindowStates windowStateBeforeMonitoringWall_;
    bool menuVisibleBeforeMonitoringWall_ = true;
    bool toolbarVisibleBeforeMonitoringWall_ = true;
    bool statusBarExistedBeforeMonitoringWall_ = false;
    bool statusBarVisibleBeforeMonitoringWall_ = false;
    bool logVisibleBeforeMonitoringWall_ = false;
    bool deviceControlVisibleBeforeMonitoringWall_ = false;
    bool eventCenterVisibleBeforeMonitoringWall_ = false;
    bool monitoringWallMode_ = false;
    bool wasVisibleBeforeFullscreen_ = false;
    FullscreenPresentationMode fullscreenPresentationMode_ =
        FullscreenPresentationMode::TemporaryWindowCanvas;
};
