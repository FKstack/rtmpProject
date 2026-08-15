#pragma once

#include <QObject>

#include <cstdint>
#include <memory>

#include "media/AudioPacketSink.h"
#include "media/AudioPlaybackObserver.h"
#include "media/PlaybackTypes.h"

class LatestFrameMailbox;

/**
 * Single-output AAC playback service. Decoder and QAudioSink live exclusively
 * on a dedicated Qt event-loop thread; network callers enter through a bounded
 * ownership-transfer queue.
 */
class AudioPlaybackEngine final : public QObject, public AudioPacketSink
{
    Q_OBJECT

public:
    explicit AudioPlaybackEngine(QObject *parent = nullptr);
    ~AudioPlaybackEngine() override;

    AudioPlaybackEngine(const AudioPlaybackEngine &) = delete;
    AudioPlaybackEngine &operator=(const AudioPlaybackEngine &) = delete;

    void selectStream(StreamId streamId);
    void clearSelection();
    void setMuted(bool muted);
    [[nodiscard]] StreamId selectedStream() const noexcept;
    [[nodiscard]] bool isMuted() const noexcept;
    [[nodiscard]] AudioPlaybackState state(StreamId streamId) const;
    [[nodiscard]] AudioPlaybackMetrics metricsSnapshot() const;
    /** Developer qualification seam; production leaves the weak observer empty. */
    void setQualificationObserver(
        std::weak_ptr<AudioPlaybackObserver> observer
    );
    void stop();
    void setVideoClockSource(
        StreamId streamId,
        std::shared_ptr<LatestFrameMailbox> mailbox
    );

    void submitAudioConfiguration(
        StreamId streamId,
        const std::shared_ptr<void> &configuration,
        std::uint64_t sessionId
    ) override;
    void submitAudioPacket(
        StreamId streamId,
        void *packet,
        qint64 receivedMonotonicMs,
        std::uint64_t sessionId
    ) override;
    void invalidateAudioSession(
        StreamId streamId,
        std::uint64_t sessionId
    ) override;

signals:
    void stateChanged(StreamId streamId, AudioPlaybackState state);
    void metricsChanged(const AudioPlaybackMetrics &metrics);

private:
    struct Runtime;
    std::unique_ptr<Runtime> runtime_;
};
