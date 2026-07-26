#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

class QThread;

/**
 * @brief 在专用线程中拉取并解码一路 RTMP/H.264 视频。
 *
 * FFmpeg 的网络、解复用、解码和像素转换均在内部 QThread 中运行。公开信号在
 * FFmpegPlayer 所在线程投递，接收方可以安全地在 Qt UI 线程更新控件。
 */
class FFmpegPlayer final : public QObject
{
    Q_OBJECT

public:
    enum class PlaybackState {
        Stopped,
        Connecting,
        Playing,
        Reconnecting,
    };
    //把 PlaybackState 注册到 Qt 元对象系统。
    Q_ENUM(PlaybackState)

    explicit FFmpegPlayer(QObject *parent = nullptr);
    ~FFmpegPlayer() override;

    FFmpegPlayer(const FFmpegPlayer &) = delete;
    FFmpegPlayer &operator=(const FFmpegPlayer &) = delete;
    FFmpegPlayer(FFmpegPlayer &&) = delete;
    FFmpegPlayer &operator=(FFmpegPlayer &&) = delete;

    /**
     * @brief 启动一路 RTMP 播放。
     * @return URL 合法且线程成功启动时返回 true；已运行或 URL 非 RTMP 时返回 false。
     * @thread 必须在 FFmpegPlayer 所在线程调用。
     */
    bool start(const QString &rtmpUrl);

    /**
     * @brief 非阻塞地请求内部解码线程停止。
     *
     * 该函数只发布原子停止标志并唤醒重连等待，不等待线程退出。多路管理器可先向
     * 全部播放器发布请求，再逐路调用 stop() 等待，从而并行中断网络读取。
     *
     * @thread 必须在 FFmpegPlayer 所在线程调用；可重复调用。
     */
    void requestStop();

    /**
     * @brief 请求停止并等待内部线程释放所有 FFmpeg 资源。
     * @thread 必须在 FFmpegPlayer 所在线程调用；可重复调用。
     */
    void stop();

    /**
     * @brief 判断内部解码线程是否仍在运行。
     * @thread 必须在 FFmpegPlayer 所在线程调用。
     */
    [[nodiscard]] bool isRunning() const noexcept;

signals:
    void frameReady(const QImage &image);
    void stateChanged(FFmpegPlayer::PlaybackState state);
    void errorOccurred(const QString &message);

private:
    void decodeLoop(QString rtmpUrl, std::uint64_t sessionId);
    void enqueueFrame(QImage image, std::uint64_t sessionId);
    void deliverLatestFrame(std::uint64_t sessionId);
    void postState(PlaybackState state, std::uint64_t sessionId);
    void postError(QString message, std::uint64_t sessionId);
    void setStateOnOwnerThread(PlaybackState state);
    [[nodiscard]] bool waitForReconnect(int delayMs);
    static int interruptCallback(void *opaque) noexcept;

    std::unique_ptr<QThread> decodeThread_;
    std::atomic_bool stopRequested_ {false};
    std::atomic_uint64_t sessionId_ {0};

    std::mutex reconnectMutex_;
    std::condition_variable reconnectCondition_;

    std::mutex frameMutex_;
    QImage pendingFrame_;
    bool frameDeliveryScheduled_ = false;

    PlaybackState state_ = PlaybackState::Stopped;
};

Q_DECLARE_METATYPE(FFmpegPlayer::PlaybackState)
