#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

#include "media/FFmpegPlayer.h"

/**
 * @brief 在所属线程中统一管理多路相互独立的 FFmpegPlayer。
 *
 * 管理器只负责播放器所有权、索引路由和批量生命周期，不执行网络读取或解码。
 * 每个 FFmpegPlayer 仍拥有自己的专用 QThread、停止标志、重连状态和最新帧邮箱。
 *
 * @thread 必须在同一个 Qt 所属线程中创建和调用；通常为 UI 线程。
 */
class MultiStreamPlaybackManager final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 为给定 URL 列表创建一组独立播放器。
     *
     * @param streamUrls 按稳定索引排列的 RTMP URL；每个 URL 对应一个播放器。
     * @param parent Qt 父对象；管理器销毁前会停止并等待全部播放器。
     * @thread 必须在管理器所属线程中调用。
     */
    explicit MultiStreamPlaybackManager(
        const QStringList &streamUrls,
        QObject *parent = nullptr
    );

    /**
     * @brief 请求并等待全部播放器退出。
     *
     * @thread 必须在管理器所属线程中调用。
     */
    ~MultiStreamPlaybackManager() override;

    MultiStreamPlaybackManager(const MultiStreamPlaybackManager &) = delete;
    MultiStreamPlaybackManager &operator=(const MultiStreamPlaybackManager &) = delete;
    MultiStreamPlaybackManager(MultiStreamPlaybackManager &&) = delete;
    MultiStreamPlaybackManager &operator=(MultiStreamPlaybackManager &&) = delete;

    /** @brief 返回管理器拥有的独立播放器数量。 */
    [[nodiscard]] int streamCount() const noexcept;

    /**
     * @brief 启动指定索引的一路播放器。
     *
     * @param streamIndex 从 0 开始的稳定流索引。
     * @return 索引有效且播放器成功启动时返回 true。
     * @thread 必须在管理器所属线程中调用。
     */
    bool startStream(int streamIndex);

    /**
     * @brief 停止并等待指定索引的一路播放器退出。
     *
     * 无效索引会被忽略，其他路不会受到影响。
     *
     * @param streamIndex 从 0 开始的稳定流索引。
     * @thread 必须在管理器所属线程中调用；可重复调用。
     */
    void stopStream(int streamIndex);

    /**
     * @brief 依次启动全部播放器，并在单路失败后继续启动其余路。
     *
     * @return 本次成功启动的播放器数量。
     * @thread 必须在管理器所属线程中调用。
     */
    int startAll();

    /**
     * @brief 并行发布停止请求，再逐路等待全部播放器退出。
     *
     * @thread 必须在管理器所属线程中调用；可重复调用。
     */
    void stopAll();

    /**
     * @brief 判断指定索引的解码线程是否仍在运行。
     *
     * @param streamIndex 从 0 开始的稳定流索引。
     * @return 索引有效且该路仍在运行时返回 true。
     * @thread 必须在管理器所属线程中调用。
     */
    [[nodiscard]] bool isStreamRunning(int streamIndex) const noexcept;

signals:
    /** @brief 转发指定流索引的最新视频帧。 */
    void frameReady(int streamIndex, const QImage &image);

    /** @brief 转发指定流索引的播放状态变化。 */
    void stateChanged(int streamIndex, FFmpegPlayer::PlaybackState state);

    /** @brief 转发指定流索引的安全错误消息；消息不包含完整 URL。 */
    void errorOccurred(int streamIndex, const QString &message);

private:
    [[nodiscard]] FFmpegPlayer *playerAt(int streamIndex) const noexcept;

    QStringList streamUrls_;
    std::vector<std::unique_ptr<FFmpegPlayer>> players_;
};
