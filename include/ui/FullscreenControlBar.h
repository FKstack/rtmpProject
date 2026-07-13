#pragma once

#include <QFrame>
#include <QString>

class QEnterEvent;
class QEvent;
class QLabel;
class QPushButton;

/**
 * @brief 全屏预览窗口底部的悬浮控制栏。
 *
 * 控制栏作为 FullscreenVideoWindow 的覆盖层存在，不参与视频渲染区域的布局计算。
 * 静音和截图当前只发出意图信号，后续由播放器控制器接入实际业务。
 *
 * @thread 仅允许在 Qt UI 线程中创建和访问。
 */
class FullscreenControlBar final : public QFrame
{
    Q_OBJECT

public:
    /**
     * @brief 创建包含设备信息和预留操作入口的悬浮控制栏。
     *
     * @param parent Qt 父对象；通常为 FullscreenVideoWindow。
     * @thread 必须在 Qt UI 线程中调用。
     */
    explicit FullscreenControlBar(QWidget *parent = nullptr);

    /**
     * @brief 更新控制栏显示的设备名称。
     *
     * @param deviceName 当前全屏视频所属的设备名称。
     * @thread 必须在 Qt UI 线程中调用。
     */
    void setDeviceName(const QString &deviceName);

    /**
     * @brief 更新播放状态或分辨率等信息占位文本。
     *
     * @param streamInfo 要显示的状态或流信息。
     * @thread 必须在 Qt UI 线程中调用。
     */
    void setStreamInfo(const QString &streamInfo);

signals:
    /** @brief 用户请求退出当前全屏预览。 */
    void exitRequested();

    /** @brief 用户请求切换静音状态；当前仅保留给后续播放器控制器。 */
    void muteRequested();

    /** @brief 用户请求截取当前画面；当前仅保留给后续播放器控制器。 */
    void screenshotRequested();

    /** @brief 鼠标进入控制栏范围，用于暂停自动隐藏计时。 */
    void pointerEntered();

    /** @brief 鼠标离开控制栏范围，用于恢复自动隐藏计时。 */
    void pointerLeft();

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QLabel *deviceNameLabel_ = nullptr;
    QLabel *streamInfoLabel_ = nullptr;
    QPushButton *exitButton_ = nullptr;
    QPushButton *muteButton_ = nullptr;
    QPushButton *screenshotButton_ = nullptr;
};
