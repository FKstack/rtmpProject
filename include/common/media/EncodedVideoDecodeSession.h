#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

#include "h264/H264MediaContracts.h"
#include "media/PlaybackTypes.h"

class DecodeWorkerPool;
class LatestFrameMailbox;
struct EncodedVideoDecodeSessionFfmpegAccess;

/**
 * @brief Owns one H.264 decoder, bounded compressed queue and frame mailbox.
 *
 * A DecodeWorkerPool supplies thread affinity but does not own this session.
 * submit() is thread-safe. Generation start/close and metricsSnapshot() are
 * control-thread operations. close() is idempotent and waits for queued decode
 * work before releasing FFmpeg state. State/error callbacks are observations:
 * they may run on the decode worker and must not synchronously call close().
 */
class EncodedVideoDecodeSession final
{
public:
    using StateCallback =
        std::function<void(DeviceStatus, std::uint64_t)>;
    using ErrorCallback =
        std::function<void(PlaybackError, std::uint64_t)>;

    EncodedVideoDecodeSession(
        StreamId streamId,
        QString displayName,
        DecodeWorkerPool *decodeWorkerPool,
        PlaybackPerformanceOptions options,
        StateCallback stateCallback = {},
        ErrorCallback errorCallback = {}
    );
    ~EncodedVideoDecodeSession();

    EncodedVideoDecodeSession(const EncodedVideoDecodeSession &) = delete;
    EncodedVideoDecodeSession &operator=(
        const EncodedVideoDecodeSession &
    ) = delete;

    /** Starts a generation that accepts complete Annex-B H.264 access units. */
    bool beginExternalGeneration(std::uint64_t generation);

    /** Thread-safe bounded ingress for a transport or publisher composition. */
    [[nodiscard]] H264SubmitResult submit(SessionMediaSample sample);

    /** Closes only when generation is still current; safe to call repeatedly. */
    void closeGeneration(std::uint64_t generation);

    /** Unconditionally closes the current generation; safe to call repeatedly. */
    void close();

    [[nodiscard]] std::uint64_t activeGeneration() const noexcept;
    [[nodiscard]] std::shared_ptr<LatestFrameMailbox> frameMailbox() const;
    StreamMetrics metricsSnapshot(DeviceStatus state);
    void recordReconnect() noexcept;

private:
    friend struct EncodedVideoDecodeSessionFfmpegAccess;

    struct State;

    void attachDecodeWorkerPool(DecodeWorkerPool *decodeWorkerPool);
    void prepareGeneration(std::uint64_t generation, bool waitForKeyframe);
    void configureOpaque(
        const std::shared_ptr<void> &codecConfiguration,
        std::uint64_t generation,
        bool waitForKeyframe
    );
    H264SubmitResult submitOpaquePacket(
        void *packet,
        qint64 receivedMonotonicMs,
        std::uint64_t generation
    );
    static void scheduleDecodeLocked(const std::shared_ptr<State> &state);
    static void drainDecodeSession(const std::shared_ptr<State> &state);

    std::shared_ptr<State> state_;
};
