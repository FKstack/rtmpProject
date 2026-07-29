#pragma once

#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

#include "media/DecodeWorkerPool.h"
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

    void setPresentationTarget(
        StreamId streamId,
        const PresentationTarget &target
    );
    StreamMetrics streamMetrics(StreamId streamId);
    QList<StreamMetrics> metricsSnapshot();

    /** @brief 每秒原子写入无 URL 的 JSON 指标；空路径关闭输出。 */
    void setMetricsOutputPath(const QString &path);
    [[nodiscard]] QString metricsOutputPath() const;

signals:
    void frameReady(StreamId streamId, const PresentableVideoFrame &frame);
    void stateChanged(StreamId streamId, DeviceStatus state);
    void errorOccurred(StreamId streamId, const PlaybackError &error);
    void reconnectScheduled(
        StreamId streamId,
        int consecutiveFailures,
        int delayMs
    );
    void metricsUpdated(StreamId streamId, const StreamMetrics &metrics);

private:
    struct Entry;

    [[nodiscard]] Entry *entryFor(StreamId streamId) noexcept;
    [[nodiscard]] const Entry *entryFor(StreamId streamId) const noexcept;
    void presentLatestFrames();
    void publishMetrics();
    void writeMetricsFile(const QList<StreamMetrics> &metrics);

    PlaybackPerformanceOptions options_;
    std::unique_ptr<DecodeWorkerPool> decodeWorkerPool_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::unique_ptr<QTimer> presentationTimer_;
    std::unique_ptr<QTimer> metricsTimer_;
    StreamId nextStreamId_ = 1;
    QString metricsOutputPath_;
    qint64 lastPresentationTickMs_ = 0;
    qint64 maximumUiTimerGapMs_ = 0;
};
