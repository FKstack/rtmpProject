#include "media/MultiStreamPlaybackManager.h"

#include <QThread>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

constexpr int kMaximumStreams = 16;

int resolveWorkerCount(const PlaybackPerformanceOptions &options)
{
    if (options.decodeWorkerCount > 0) {
        return std::clamp(options.decodeWorkerCount, 1, 16);
    }
    const int ideal = QThread::idealThreadCount();
    return std::clamp(ideal > 0 ? ideal / 2 : 1, 1, 8);
}

} // namespace

struct MultiStreamPlaybackManager::Entry
{
    StreamConnection connection;
    std::unique_ptr<FFmpegPlayer> player;
};

MultiStreamPlaybackManager::MultiStreamPlaybackManager(
    PlaybackPerformanceOptions options,
    QObject *parent
)
    : QObject(parent)
    , options_(options)
    , decodeWorkerPool_(
          std::make_unique<DecodeWorkerPool>(resolveWorkerCount(options_))
      )
    , audioPlaybackEngine_(std::make_unique<AudioPlaybackEngine>())
    , metricsTimer_(std::make_unique<QTimer>())
{
    qRegisterMetaType<StreamMetrics>();
    qRegisterMetaType<PlaybackError>();
    qRegisterMetaType<AudioPlaybackState>();
    qRegisterMetaType<AudioPlaybackMetrics>();

    connect(
        audioPlaybackEngine_.get(),
        &AudioPlaybackEngine::stateChanged,
        this,
        &MultiStreamPlaybackManager::audioStateChanged
    );
    connect(
        audioPlaybackEngine_.get(),
        &AudioPlaybackEngine::metricsChanged,
        this,
        &MultiStreamPlaybackManager::audioMetricsUpdated
    );

    metricsTimer_->setInterval(1'000);
    connect(
        metricsTimer_.get(),
        &QTimer::timeout,
        this,
        &MultiStreamPlaybackManager::publishMetrics
    );
    metricsTimer_->start();

}

MultiStreamPlaybackManager::MultiStreamPlaybackManager(
    const QStringList &streamUrls,
    QObject *parent
)
    : MultiStreamPlaybackManager(PlaybackPerformanceOptions {}, parent)
{
    for (int index = 0;
         index < streamUrls.size() && index < kMaximumStreams;
         ++index) {
        addStream(
            QStringLiteral("Camera %1")
                .arg(index + 1, 2, 10, QLatin1Char('0')),
            streamUrls.at(index)
        );
    }
}

MultiStreamPlaybackManager::~MultiStreamPlaybackManager()
{
    metricsTimer_->stop();
    stopAll();
    entries_.clear();
    audioPlaybackEngine_->stop();
    decodeWorkerPool_->stop();
}

int MultiStreamPlaybackManager::streamCount() const noexcept
{
    return static_cast<int>(entries_.size());
}

int MultiStreamPlaybackManager::decodeWorkerCount() const noexcept
{
    return decodeWorkerPool_->workerCount();
}

QList<StreamId> MultiStreamPlaybackManager::streamIds() const
{
    QList<StreamId> result;
    result.reserve(streamCount());
    for (const auto &entry : entries_) {
        result.append(entry->connection.id);
    }
    return result;
}

StreamId MultiStreamPlaybackManager::addStream(
    const QString &displayName,
    const QString &rtmpUrl
)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (streamCount() >= kMaximumStreams || displayName.trimmed().isEmpty()) {
        return kInvalidStreamId;
    }

    auto entry = std::make_unique<Entry>();
    entry->connection = {
        nextStreamId_++,
        displayName.trimmed(),
        rtmpUrl.trimmed()
    };
    entry->player = std::make_unique<FFmpegPlayer>(
        entry->connection.id,
        entry->connection.displayName,
        decodeWorkerPool_.get(),
        options_
    );
    entry->player->setAudioPacketSink(audioPlaybackEngine_.get());
    audioPlaybackEngine_->setVideoClockSource(
        entry->connection.id, entry->player->frameMailbox()
    );
    entry->player->setObjectName(
        QStringLiteral("streamPlayer%1").arg(entry->connection.id)
    );
    const StreamId streamId = entry->connection.id;
    connect(
        entry->player.get(),
        &FFmpegPlayer::stateChanged,
        this,
        [this, streamId](DeviceStatus state) {
            emit stateChanged(streamId, state);
        }
    );
    connect(
        entry->player.get(),
        &FFmpegPlayer::errorOccurred,
        this,
        [this, streamId](const PlaybackError &error) {
            emit errorOccurred(streamId, error);
        }
    );
    connect(
        entry->player.get(),
        &FFmpegPlayer::reconnectScheduled,
        this,
        [this, streamId](int consecutiveFailures, int delayMs) {
            emit reconnectScheduled(
                streamId,
                consecutiveFailures,
                delayMs
            );
        }
    );
    entries_.push_back(std::move(entry));
    return streamId;
}

bool MultiStreamPlaybackManager::removeStream(StreamId streamId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto iterator = std::find_if(
        entries_.begin(),
        entries_.end(),
        [streamId](const auto &entry) {
            return entry->connection.id == streamId;
        }
    );
    if (iterator == entries_.end()) {
        return false;
    }
    if (audioPlaybackEngine_->selectedStream() == streamId) {
        audioPlaybackEngine_->clearSelection();
    }
    audioPlaybackEngine_->setVideoClockSource(streamId, nullptr);
    (*iterator)->player->stop();
    entries_.erase(iterator);
    return true;
}

bool MultiStreamPlaybackManager::restartStream(StreamId streamId)
{
    Entry *entry = entryFor(streamId);
    if (entry == nullptr) {
        return false;
    }
    entry->player->stop();
    return entry->player->start(entry->connection.url);
}

bool MultiStreamPlaybackManager::startStream(StreamId streamId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    Entry *entry = entryFor(streamId);
    return entry != nullptr && entry->player->start(entry->connection.url);
}

void MultiStreamPlaybackManager::stopStream(StreamId streamId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (Entry *entry = entryFor(streamId); entry != nullptr) {
        entry->player->stop();
        if (audioPlaybackEngine_->selectedStream() == streamId) {
            audioPlaybackEngine_->clearSelection();
        }
    }
}

int MultiStreamPlaybackManager::startAll()
{
    Q_ASSERT(QThread::currentThread() == thread());
    int count = 0;
    for (const auto &entry : entries_) {
        if (entry->player->start(entry->connection.url)) {
            ++count;
        }
    }
    return count;
}

void MultiStreamPlaybackManager::stopAll()
{
    Q_ASSERT(QThread::currentThread() == thread());
    for (const auto &entry : entries_) {
        entry->player->requestStop();
    }
    for (const auto &entry : entries_) {
        entry->player->stop();
    }
    audioPlaybackEngine_->clearSelection();
}

bool MultiStreamPlaybackManager::isStreamRunning(StreamId streamId) const noexcept
{
    Q_ASSERT(QThread::currentThread() == thread());
    const Entry *entry = entryFor(streamId);
    return entry != nullptr && entry->player->isRunning();
}

std::shared_ptr<LatestFrameMailbox>
MultiStreamPlaybackManager::frameMailbox(StreamId streamId) const
{
    const Entry *entry = entryFor(streamId);
    return entry != nullptr ? entry->player->frameMailbox() : nullptr;
}

StreamMetrics MultiStreamPlaybackManager::streamMetrics(StreamId streamId)
{
    if (Entry *entry = entryFor(streamId); entry != nullptr) {
        return entry->player->metricsSnapshot();
    }
    return {};
}

QList<StreamMetrics> MultiStreamPlaybackManager::metricsSnapshot()
{
    QList<StreamMetrics> result;
    result.reserve(streamCount());
    for (const auto &entry : entries_) {
        result.append(entry->player->metricsSnapshot());
    }
    return result;
}

bool MultiStreamPlaybackManager::selectAudioStream(StreamId streamId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (entryFor(streamId) == nullptr) return false;
    audioPlaybackEngine_->selectStream(streamId);
    return true;
}

void MultiStreamPlaybackManager::clearAudioSelection()
{
    Q_ASSERT(QThread::currentThread() == thread());
    audioPlaybackEngine_->clearSelection();
}

void MultiStreamPlaybackManager::setAudioMuted(bool muted)
{
    Q_ASSERT(QThread::currentThread() == thread());
    audioPlaybackEngine_->setMuted(muted);
}

StreamId MultiStreamPlaybackManager::selectedAudioStream() const noexcept
{
    return audioPlaybackEngine_->selectedStream();
}

bool MultiStreamPlaybackManager::isAudioMuted() const noexcept
{
    return audioPlaybackEngine_->isMuted();
}

AudioPlaybackState MultiStreamPlaybackManager::audioState(
    StreamId streamId
) const
{
    return audioPlaybackEngine_->state(streamId);
}

AudioPlaybackMetrics MultiStreamPlaybackManager::audioMetrics() const
{
    return audioPlaybackEngine_->metricsSnapshot();
}

MultiStreamPlaybackManager::Entry *
MultiStreamPlaybackManager::entryFor(StreamId streamId) noexcept
{
    const auto iterator = std::find_if(
        entries_.begin(),
        entries_.end(),
        [streamId](const auto &entry) {
            return entry->connection.id == streamId;
        }
    );
    return iterator != entries_.end() ? iterator->get() : nullptr;
}

const MultiStreamPlaybackManager::Entry *
MultiStreamPlaybackManager::entryFor(StreamId streamId) const noexcept
{
    const auto iterator = std::find_if(
        entries_.begin(),
        entries_.end(),
        [streamId](const auto &entry) {
            return entry->connection.id == streamId;
        }
    );
    return iterator != entries_.end() ? iterator->get() : nullptr;
}

void MultiStreamPlaybackManager::publishMetrics()
{
    const QList<StreamMetrics> metrics = metricsSnapshot();
    for (const StreamMetrics &streamMetrics : metrics) {
        emit metricsUpdated(streamMetrics.streamId, streamMetrics);
    }
}
