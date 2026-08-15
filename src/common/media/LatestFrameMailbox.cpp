#include "media/LatestFrameMailbox.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaximumLatencySamples = 20'000;

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           )
        .count();
}

void appendSample(std::vector<qint64> &samples, qint64 value)
{
    if (samples.size() >= kMaximumLatencySamples) {
        samples.erase(samples.begin(), samples.begin() + samples.size() / 4);
    }
    samples.push_back(value);
}

qint64 percentile(std::vector<qint64> values, double fraction)
{
    if (values.empty()) {
        return -1;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::clamp(
        std::ceil(fraction * static_cast<double>(values.size())) - 1.0,
        0.0,
        static_cast<double>(values.size() - 1)
    ));
    return values[index];
}

} // namespace

bool LatestFrameMailbox::submit(VideoFrame frame)
{
    if (!frame.isValid()) {
        return false;
    }

    std::vector<std::function<void()>> callbacks;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (latest_.isValid() &&
            (frame.sessionGeneration() < latest_.sessionGeneration() ||
             (frame.sessionGeneration() == latest_.sessionGeneration() &&
              frame.sequence() <= latest_.sequence()))) {
            ++stats_.rejectedStale;
            return false;
        }
        if (latest_.isValid() && latest_.sequence() > lastConsumedSequence_) {
            ++stats_.overwritten;
        }
        latest_ = std::move(frame);
        ++stats_.submitted;
        callbacks.reserve(subscribers_.size());
        for (const auto &entry : subscribers_) {
            callbacks.push_back(entry.second);
        }
    }
    for (const auto &callback : callbacks) {
        callback();
    }
    return true;
}

std::optional<VideoFrame> LatestFrameMailbox::latestAfter(
    std::uint64_t sequence
) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!latest_.isValid() || latest_.sequence() <= sequence) {
        return std::nullopt;
    }
    return latest_;
}

std::optional<VideoFrame> LatestFrameMailbox::consumeLatestAfter(
    std::uint64_t sequence
)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!latest_.isValid() || latest_.sequence() <= sequence) {
        return std::nullopt;
    }
    lastConsumedSequence_ = std::max(lastConsumedSequence_, latest_.sequence());
    ++stats_.consumed;
    return latest_;
}

void LatestFrameMailbox::clear()
{
    std::vector<std::function<void()>> callbacks;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        latest_ = {};
        lastPresentationMonotonicMs_ = -1;
        lastRenderedMediaTimestampMs_ = -1;
        lastRenderedSessionGeneration_ = 0;
        ++stats_.cleared;
        callbacks.reserve(subscribers_.size());
        for (const auto &entry : subscribers_) {
            callbacks.push_back(entry.second);
        }
    }
    for (const auto &callback : callbacks) {
        callback();
    }
}

void LatestFrameMailbox::recordUploaded()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.uploaded;
}

void LatestFrameMailbox::recordRendered()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.rendered;
    if (!latest_.isValid() ||
        latest_.sequence() == lastLatencySampledSequence_) {
        return;
    }
    lastLatencySampledSequence_ = latest_.sequence();
    const qint64 renderedAt = monotonicMilliseconds();
    if (lastPresentationMonotonicMs_ >= 0) {
        appendSample(
            presentationIntervalSamples_,
            std::max<qint64>(0, renderedAt - lastPresentationMonotonicMs_)
        );
    }
    lastPresentationMonotonicMs_ = renderedAt;
    const VideoRational timeBase = latest_.timeBase();
    if (timeBase.numerator > 0 && timeBase.denominator > 0) {
        lastRenderedMediaTimestampMs_ = latest_.pts() * 1000 *
            timeBase.numerator / timeBase.denominator;
        lastRenderedSessionGeneration_ = latest_.sessionGeneration();
    }
    if (latest_.sourceSequence().has_value()) {
        stats_.lastPresentedSourceSequence =
            static_cast<qint64>(*latest_.sourceSequence());
    }
    appendSample(
        internalLatencySamples_,
        std::max<qint64>(
            0, renderedAt - latest_.receivedMonotonicMs()
        )
    );
    if (latest_.sourceTimestampMs() >= 0) {
        const qint64 latency = QDateTime::currentMSecsSinceEpoch() -
                               latest_.sourceTimestampMs();
        if (latency >= 0 && latency <= 10'000) {
            appendSample(sourceLatencySamples_, latency);
        }
    }
}

qint64 LatestFrameMailbox::lastRenderedMediaTimestampMs(
    std::uint64_t sessionGeneration
) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return lastRenderedSessionGeneration_ == sessionGeneration
               ? lastRenderedMediaTimestampMs_
               : -1;
}

qint64 LatestFrameMailbox::audioSyncMediaTimestampMs(
    std::uint64_t sessionGeneration,
    qint64 renderedClockStaleAfterMs
) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const qint64 now = monotonicMilliseconds();
    if (lastRenderedSessionGeneration_ == sessionGeneration &&
        lastRenderedMediaTimestampMs_ >= 0 &&
        lastPresentationMonotonicMs_ >= 0 &&
        now - lastPresentationMonotonicMs_ <= renderedClockStaleAfterMs) {
        return lastRenderedMediaTimestampMs_;
    }
    if (!latest_.isValid() ||
        latest_.sessionGeneration() != sessionGeneration) {
        return -1;
    }
    const VideoRational timeBase = latest_.timeBase();
    if (timeBase.numerator <= 0 || timeBase.denominator <= 0) return -1;
    return latest_.pts() * 1000 * timeBase.numerator / timeBase.denominator;
}

void LatestFrameMailbox::setRenderDiagnostics(
    qint64 uploadCpuUs,
    qint64 paintCpuUs,
    std::uint64_t dirtyMerges,
    std::uint64_t scheduleChecks,
    qint64 textureBytes
)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    stats_.uploadCpuUs = uploadCpuUs;
    stats_.paintCpuUs = paintCpuUs;
    stats_.dirtyMerges = dirtyMerges;
    stats_.scheduleChecks = scheduleChecks;
    stats_.textureBytes = textureBytes;
}

LatestFrameMailboxStats LatestFrameMailbox::stats() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    LatestFrameMailboxStats result = stats_;
    result.internalLatencyP95Ms = percentile(internalLatencySamples_, 0.95);
    result.sourceLatencyP50Ms = percentile(sourceLatencySamples_, 0.50);
    result.sourceLatencyP95Ms = percentile(sourceLatencySamples_, 0.95);
    result.sourceLatencyMaxMs = sourceLatencySamples_.empty()
                                    ? -1
                                    : *std::max_element(
                                          sourceLatencySamples_.begin(),
                                          sourceLatencySamples_.end()
                                      );
    result.sourceLatencySamples = sourceLatencySamples_.size();
    result.presentationIntervalP50Ms = percentile(
        presentationIntervalSamples_, 0.50
    );
    result.presentationIntervalP95Ms = percentile(
        presentationIntervalSamples_, 0.95
    );
    result.presentationIntervalMaxMs =
        presentationIntervalSamples_.empty()
            ? -1
            : *std::max_element(
                  presentationIntervalSamples_.begin(),
                  presentationIntervalSamples_.end()
              );
    return result;
}

std::uint64_t LatestFrameMailbox::latestSequence() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return latest_.isValid() ? latest_.sequence() : 0;
}

qint64 LatestFrameMailbox::lastPresentedFrameAgeMs() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (lastPresentationMonotonicMs_ < 0) {
        return -1;
    }
    return std::max<qint64>(
        0,
        monotonicMilliseconds() - lastPresentationMonotonicMs_
    );
}

LatestFrameMailbox::SubscriberId LatestFrameMailbox::subscribe(
    std::function<void()> callback
)
{
    if (!callback) {
        return 0;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    const SubscriberId id = nextSubscriberId_++;
    subscribers_.emplace(id, std::move(callback));
    return id;
}

void LatestFrameMailbox::unsubscribe(SubscriberId id)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(id);
}
