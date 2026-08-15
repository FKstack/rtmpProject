#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

#include "media/DecodeWorkerPool.h"
#include "media/AudioPacketSink.h"
#include "media/LatestFrameMailbox.h"
#include "media/PlaybackTypes.h"

class QThread;

/**
 * @brief 一路 RTMP/H.264 的网络会话与共享解码池入口。
 *
 * 每个实例拥有独立阻塞网络线程；压缩包通过有界队列交给外部 DecodeWorkerPool。
 * 管理器模式关闭逐帧 Qt 投递并使用统一展示定时器轮询最新帧。
 */
class FFmpegPlayer final : public QObject
{
    Q_OBJECT

public:
    /** @brief 创建带一个私有解码 worker 的兼容单路播放器。 */
    explicit FFmpegPlayer(QObject *parent = nullptr);

    /** @brief 创建使用共享解码池的播放器。 */
    FFmpegPlayer(
        StreamId streamId,
        QString displayName,
        DecodeWorkerPool *decodeWorkerPool,
        PlaybackPerformanceOptions options,
        QObject *parent = nullptr
    );

    ~FFmpegPlayer() override;

    FFmpegPlayer(const FFmpegPlayer &) = delete;
    FFmpegPlayer &operator=(const FFmpegPlayer &) = delete;

    bool start(const QString &rtmpUrl);
    void requestStop();
    void stop();
    void setAudioPacketSink(AudioPacketSink *sink) noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

    [[nodiscard]] std::shared_ptr<LatestFrameMailbox> frameMailbox() const;

    /** @brief 复制最新帧；调用方用 sequence 判断是否已经展示。 */

    /** @brief 在 UI 接收最新帧时更新展示与延迟统计。 */

    /** @brief 生成当前累计计数和最近采样速率。 */
    StreamMetrics metricsSnapshot();

signals:
    void stateChanged(DeviceStatus state);
    void errorOccurred(const PlaybackError &error);
    void reconnectScheduled(int consecutiveFailures, int delayMs);

private:
    struct StreamDecodeSession;

    static void drainDecodeSession(
        const std::shared_ptr<StreamDecodeSession> &session
    );
    void decodeNetworkLoop(QString rtmpUrl, std::uint64_t sessionId);
    void enqueueDecoderConfiguration(
        const std::shared_ptr<void> &codecConfiguration,
        std::uint64_t sessionId
    );
    void enqueuePacket(
        void *packet,
        qint64 receivedMonotonicMs,
        std::uint64_t sessionId
    );
    void enqueueAudioConfiguration(
        const std::shared_ptr<void> &codecConfiguration,
        std::uint64_t sessionId
    );
    void enqueueAudioPacket(
        void *packet,
        qint64 receivedMonotonicMs,
        std::uint64_t sessionId
    );
    void scheduleDecodeLocked(
        const std::shared_ptr<StreamDecodeSession> &session
    );
    void postState(DeviceStatus state, std::uint64_t sessionId);
    void postError(PlaybackError error, std::uint64_t sessionId);
    void postReconnectScheduled(
        int consecutiveFailures,
        int delayMs,
        std::uint64_t sessionId
    );
    void setStateOnOwnerThread(DeviceStatus state);
    [[nodiscard]] bool waitForReconnect(int delayMs);

    std::unique_ptr<DecodeWorkerPool> ownedDecodeWorkerPool_;
    DecodeWorkerPool *decodeWorkerPool_ = nullptr;
    std::shared_ptr<StreamDecodeSession> decodeSession_;
    std::unique_ptr<QThread> networkThread_;
    AudioPacketSink *audioPacketSink_ = nullptr;

    StreamId streamId_ = 1;
    QString displayName_ = QStringLiteral("Camera 01");
    PlaybackPerformanceOptions options_;
    std::atomic_bool stopRequested_ {false};
    std::atomic_bool restartRequested_ {false};
    std::atomic_uint64_t sessionId_ {0};

    std::mutex reconnectMutex_;
    std::condition_variable reconnectCondition_;

    DeviceStatus state_ = DeviceStatus::Disconnected;
    qint64 lastMetricsSampleMs_ = 0;
    std::uint64_t lastDecodedSample_ = 0;
    std::uint64_t lastRenderedSample_ = 0;
    double decodeFps_ = 0.0;
    double displayFps_ = 0.0;
};
