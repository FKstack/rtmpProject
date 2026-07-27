#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

#include "media/DecodeWorkerPool.h"
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
    enum class PlaybackState {
        Stopped,
        Connecting,
        Playing,
        Reconnecting,
    };
    Q_ENUM(PlaybackState)

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
    [[nodiscard]] bool isRunning() const noexcept;

    void setAutomaticFrameSignalsEnabled(bool enabled) noexcept;
    void setPresentationTarget(const PresentationTarget &target);

    /** @brief 复制最新帧；调用方用 sequence 判断是否已经展示。 */
    [[nodiscard]] PresentableVideoFrame latestFrame() const;

    /** @brief 在 UI 接收最新帧时更新展示与延迟统计。 */
    void markFramePresented(const PresentableVideoFrame &frame);

    /** @brief 生成当前累计计数和最近采样速率。 */
    StreamMetrics metricsSnapshot();

signals:
    void frameReady(const QImage &image);
    void stateChanged(FFmpegPlayer::PlaybackState state);
    void errorOccurred(const QString &message);

private:
    struct SharedState;

    static void drainDecodeState(const std::shared_ptr<SharedState> &state);
    static int interruptCallback(void *opaque) noexcept;

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
    void scheduleDecodeLocked(const std::shared_ptr<SharedState> &state);
    void deliverLatestFrame(std::uint64_t sessionId);
    void postState(PlaybackState state, std::uint64_t sessionId);
    void postError(QString message, std::uint64_t sessionId);
    void setStateOnOwnerThread(PlaybackState state);
    [[nodiscard]] bool waitForReconnect(int delayMs);

    std::unique_ptr<DecodeWorkerPool> ownedDecodeWorkerPool_;
    DecodeWorkerPool *decodeWorkerPool_ = nullptr;
    std::shared_ptr<SharedState> sharedState_;
    std::unique_ptr<QThread> networkThread_;

    StreamId streamId_ = 1;
    QString displayName_ = QStringLiteral("Camera 01");
    PlaybackPerformanceOptions options_;
    std::atomic_bool stopRequested_ {false};
    std::atomic_bool restartRequested_ {false};
    std::atomic_uint64_t sessionId_ {0};

    std::mutex reconnectMutex_;
    std::condition_variable reconnectCondition_;

    PlaybackState state_ = PlaybackState::Stopped;
    std::uint64_t lastAutomaticSequence_ = 0;
    qint64 lastMetricsSampleMs_ = 0;
    std::uint64_t lastDecodedSample_ = 0;
    std::uint64_t lastPresentedSample_ = 0;
    double decodeFps_ = 0.0;
    double displayFps_ = 0.0;
};

Q_DECLARE_METATYPE(FFmpegPlayer::PlaybackState)
