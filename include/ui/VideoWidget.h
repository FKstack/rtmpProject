#pragma once

#include <QFrame>
#include <QString>

class QLabel;

/**
 * @brief 单路设备视频的显示槽位。
 *
 * 该类只管理设备名称、状态文本和黑色视频占位区域，不负责拉流、解码或跨线程
 * 帧传递。后续解码模块应通过 UI 线程的信号槽更新该控件。
 *
 * @thread 仅允许在 Qt UI 线程中创建和更新。
 */
class VideoWidget final : public QFrame
{
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

private:
    QLabel *titleLabel_ = nullptr;
    QFrame *videoSurface_ = nullptr;
    QLabel *statusLabel_ = nullptr;
};
