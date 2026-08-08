#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "media/VideoFrame.h"

struct LatestFrameMailboxStats
{
    std::uint64_t submitted = 0;
    std::uint64_t overwritten = 0;
    std::uint64_t consumed = 0;
    std::uint64_t cleared = 0;
    std::uint64_t rejectedStale = 0;
    std::uint64_t uploaded = 0;
    std::uint64_t rendered = 0;
    qint64 uploadCpuUs = 0;
    qint64 paintCpuUs = 0;
    std::uint64_t dirtyMerges = 0;
    std::uint64_t scheduleChecks = 0;
    qint64 textureBytes = 0;
    qint64 internalLatencyP95Ms = -1;
    qint64 sourceLatencyP50Ms = -1;
    qint64 sourceLatencyP95Ms = -1;
    qint64 sourceLatencyMaxMs = -1;
    std::uint64_t sourceLatencySamples = 0;
};

/**
 * @brief Capacity-one, latest-frame-wins transport between decoder and renderer.
 */
class LatestFrameMailbox final
{
public:
    using SubscriberId = std::uint64_t;

    LatestFrameMailbox() = default;
    LatestFrameMailbox(const LatestFrameMailbox &) = delete;
    LatestFrameMailbox &operator=(const LatestFrameMailbox &) = delete;

    bool submit(VideoFrame frame);
    [[nodiscard]] std::optional<VideoFrame> latestAfter(
        std::uint64_t sequence
    ) const;
    [[nodiscard]] std::optional<VideoFrame> consumeLatestAfter(
        std::uint64_t sequence
    );
    void clear();
    void recordUploaded();
    void recordRendered();
    void setRenderDiagnostics(
        qint64 uploadCpuUs,
        qint64 paintCpuUs,
        std::uint64_t dirtyMerges,
        std::uint64_t scheduleChecks,
        qint64 textureBytes
    );
    [[nodiscard]] LatestFrameMailboxStats stats() const;
    [[nodiscard]] std::uint64_t latestSequence() const;

    /** Callback executes on the submitting thread and must remain non-blocking. */
    SubscriberId subscribe(std::function<void()> callback);
    void unsubscribe(SubscriberId id);

private:
    mutable std::mutex mutex_;
    VideoFrame latest_;
    std::uint64_t lastConsumedSequence_ = 0;
    LatestFrameMailboxStats stats_;
    SubscriberId nextSubscriberId_ = 1;
    std::unordered_map<SubscriberId, std::function<void()>> subscribers_;
    std::uint64_t lastLatencySampledSequence_ = 0;
    std::vector<qint64> internalLatencySamples_;
    std::vector<qint64> sourceLatencySamples_;
};
