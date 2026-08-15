#pragma once

#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

#include "media/DecodeWorkerPool.h"
#include "media/AudioPlaybackEngine.h"
#include "media/FFmpegPlayer.h"
#include "media/PlaybackTypes.h"

class QTimer;

/**
 * @brief 动态管理 0～16 路独立网络会话和一个共享解码 worker 池。
 */
class MultiStreamPlaybackManager final : public QObject
{
    Q_OBJECT

public:
    explicit MultiStreamPlaybackManager(
        PlaybackPerformanceOptions options = {},
        QObject *parent = nullptr
    );

    /** @brief 兼容测试和批量命令行预装的构造入口。 */
    explicit MultiStreamPlaybackManager(
        const QStringList &streamUrls,
        QObject *parent = nullptr
    );

    ~MultiStreamPlaybackManager() override;

    MultiStreamPlaybackManager(const MultiStreamPlaybackManager &) = delete;
    MultiStreamPlaybackManager &operator=(const MultiStreamPlaybackManager &) = delete;

    [[nodiscard]] int streamCount() const noexcept;
    [[nodiscard]] int decodeWorkerCount() const noexcept;
    [[nodiscard]] QList<StreamId> streamIds() const;

    StreamId addStream(const QString &displayName, const QString &rtmpUrl);
    bool removeStream(StreamId streamId);
    bool restartStream(StreamId streamId);
    bool startStream(StreamId streamId);
    void stopStream(StreamId streamId);
    int startAll();
    void stopAll();
    [[nodiscard]] bool isStreamRunning(StreamId streamId) const noexcept;

    [[nodiscard]] std::shared_ptr<LatestFrameMailbox> frameMailbox(
        StreamId streamId
    ) const;
    StreamMetrics streamMetrics(StreamId streamId);
    QList<StreamMetrics> metricsSnapshot();

    bool selectAudioStream(StreamId streamId);
    void clearAudioSelection();
    void setAudioMuted(bool muted);
    [[nodiscard]] StreamId selectedAudioStream() const noexcept;
    [[nodiscard]] bool isAudioMuted() const noexcept;
    [[nodiscard]] AudioPlaybackState audioState(StreamId streamId) const;
    [[nodiscard]] AudioPlaybackMetrics audioMetrics() const;

signals:
    void stateChanged(StreamId streamId, DeviceStatus state);
    void errorOccurred(StreamId streamId, const PlaybackError &error);
    void reconnectScheduled(
        StreamId streamId,
        int consecutiveFailures,
        int delayMs
    );
    void metricsUpdated(StreamId streamId, const StreamMetrics &metrics);
    void audioStateChanged(StreamId streamId, AudioPlaybackState state);
    void audioMetricsUpdated(const AudioPlaybackMetrics &metrics);

private:
    struct Entry;

    [[nodiscard]] Entry *entryFor(StreamId streamId) noexcept;
    [[nodiscard]] const Entry *entryFor(StreamId streamId) const noexcept;
    void publishMetrics();

    PlaybackPerformanceOptions options_;
    std::unique_ptr<DecodeWorkerPool> decodeWorkerPool_;
    std::unique_ptr<AudioPlaybackEngine> audioPlaybackEngine_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::unique_ptr<QTimer> metricsTimer_;
    StreamId nextStreamId_ = 1;
};
