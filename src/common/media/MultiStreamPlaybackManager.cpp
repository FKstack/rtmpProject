#include "media/MultiStreamPlaybackManager.h"

#include "EncodedVideoInputControl.h"
#include "media/EncodedVideoDecodeSession.h"

#include <QMetaObject>
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
    std::shared_ptr<EncodedVideoDecodeSession> externalDecodeSession;
    std::shared_ptr<EncodedVideoInputControl> externalControl;
    DeviceStatus externalState = DeviceStatus::Disconnected;

    [[nodiscard]] bool isExternal() const noexcept
    {
        return externalDecodeSession != nullptr;
    }
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

EncodedVideoInputHandle MultiStreamPlaybackManager::createEncodedVideoInput(
    const QString &displayName
)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QString normalizedName = displayName.trimmed();
    if (streamCount() >= kMaximumStreams || normalizedName.isEmpty()) {
        return {};
    }

    auto entry = std::make_unique<Entry>();
    entry->connection = {
        nextStreamId_++,
        normalizedName,
        QString()
    };
    const StreamId streamId = entry->connection.id;
    const std::uint64_t generation = nextExternalGeneration_++;
    entry->externalDecodeSession =
        std::make_shared<EncodedVideoDecodeSession>(
            streamId,
            normalizedName,
            decodeWorkerPool_.get(),
            options_,
            [this, streamId](DeviceStatus state, std::uint64_t current) {
                postExternalState(streamId, current, state);
            },
            [this, streamId](PlaybackError error, std::uint64_t current) {
                postExternalError(
                    streamId, current, std::move(error)
                );
            }
        );
    entry->externalControl = std::make_shared<EncodedVideoInputControl>();
    entry->externalControl->session = entry->externalDecodeSession;
    entry->externalControl->streamId = streamId;
    entry->externalControl->generation = generation;
    audioPlaybackEngine_->setVideoClockSource(
        streamId, entry->externalDecodeSession->frameMailbox()
    );

    auto control = entry->externalControl;
    entries_.push_back(std::move(entry));
    if (!entryFor(streamId)->externalDecodeSession->beginExternalGeneration(
            generation
        )) {
        removeStream(streamId);
        return {};
    }
    return EncodedVideoInputHandle(std::move(control));
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
    if ((*iterator)->isExternal()) {
        (*iterator)->externalControl->closed.store(
            true, std::memory_order_release
        );
        (*iterator)->externalDecodeSession->closeGeneration(
            (*iterator)->externalControl->generation
        );
    } else {
        (*iterator)->player->stop();
    }
    entries_.erase(iterator);
    return true;
}

bool MultiStreamPlaybackManager::restartStream(StreamId streamId)
{
    Entry *entry = entryFor(streamId);
    if (entry == nullptr) {
        return false;
    }
    if (entry->isExternal()) {
        return false;
    }
    entry->player->stop();
    return entry->player->start(entry->connection.url);
}

bool MultiStreamPlaybackManager::startStream(StreamId streamId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    Entry *entry = entryFor(streamId);
    return entry != nullptr && !entry->isExternal() &&
           entry->player->start(entry->connection.url);
}

void MultiStreamPlaybackManager::stopStream(StreamId streamId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (Entry *entry = entryFor(streamId); entry != nullptr) {
        if (entry->isExternal()) {
            entry->externalControl->closed.store(
                true, std::memory_order_release
            );
            entry->externalDecodeSession->closeGeneration(
                entry->externalControl->generation
            );
        } else {
            entry->player->stop();
        }
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
        if (!entry->isExternal() &&
            entry->player->start(entry->connection.url)) {
            ++count;
        }
    }
    return count;
}

void MultiStreamPlaybackManager::stopAll()
{
    Q_ASSERT(QThread::currentThread() == thread());
    for (const auto &entry : entries_) {
        if (!entry->isExternal()) {
            entry->player->requestStop();
        }
    }
    for (const auto &entry : entries_) {
        if (entry->isExternal()) {
            entry->externalControl->closed.store(
                true, std::memory_order_release
            );
            entry->externalDecodeSession->closeGeneration(
                entry->externalControl->generation
            );
        } else {
            entry->player->stop();
        }
    }
    audioPlaybackEngine_->clearSelection();
}

bool MultiStreamPlaybackManager::isStreamRunning(StreamId streamId) const noexcept
{
    Q_ASSERT(QThread::currentThread() == thread());
    const Entry *entry = entryFor(streamId);
    if (entry == nullptr) {
        return false;
    }
    return entry->isExternal()
        ? entry->externalDecodeSession->activeGeneration() != 0
        : entry->player->isRunning();
}

std::shared_ptr<LatestFrameMailbox>
MultiStreamPlaybackManager::frameMailbox(StreamId streamId) const
{
    const Entry *entry = entryFor(streamId);
    if (entry == nullptr) {
        return nullptr;
    }
    return entry->isExternal()
        ? entry->externalDecodeSession->frameMailbox()
        : entry->player->frameMailbox();
}

StreamMetrics MultiStreamPlaybackManager::streamMetrics(StreamId streamId)
{
    if (Entry *entry = entryFor(streamId); entry != nullptr) {
        return entry->isExternal()
            ? entry->externalDecodeSession->metricsSnapshot(
                  entry->externalState
              )
            : entry->player->metricsSnapshot();
    }
    return {};
}

QList<StreamMetrics> MultiStreamPlaybackManager::metricsSnapshot()
{
    QList<StreamMetrics> result;
    result.reserve(streamCount());
    for (const auto &entry : entries_) {
        result.append(
            entry->isExternal()
                ? entry->externalDecodeSession->metricsSnapshot(
                      entry->externalState
                  )
                : entry->player->metricsSnapshot()
        );
    }
    return result;
}

bool MultiStreamPlaybackManager::selectAudioStream(StreamId streamId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const Entry *entry = entryFor(streamId);
    if (entry == nullptr || entry->isExternal()) return false;
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

void MultiStreamPlaybackManager::postExternalState(
    StreamId streamId,
    std::uint64_t generation,
    DeviceStatus state
)
{
    QMetaObject::invokeMethod(
        this,
        [this, streamId, generation, state] {
            Entry *entry = entryFor(streamId);
            if (entry == nullptr || !entry->isExternal() ||
                entry->externalControl->generation != generation) {
                return;
            }
            const std::uint64_t active =
                entry->externalDecodeSession->activeGeneration();
            if (state != DeviceStatus::Disconnected &&
                active != generation) {
                return;
            }
            if (entry->externalState != state) {
                entry->externalState = state;
                emit stateChanged(streamId, state);
            }
        },
        Qt::QueuedConnection
    );
}

void MultiStreamPlaybackManager::postExternalError(
    StreamId streamId,
    std::uint64_t generation,
    PlaybackError error
)
{
    QMetaObject::invokeMethod(
        this,
        [this, streamId, generation, error = std::move(error)] {
            Entry *entry = entryFor(streamId);
            if (entry == nullptr || !entry->isExternal() ||
                entry->externalControl->generation != generation ||
                entry->externalDecodeSession->activeGeneration() !=
                    generation) {
                return;
            }
            if (entry->externalState != DeviceStatus::Error) {
                entry->externalState = DeviceStatus::Error;
                emit stateChanged(streamId, DeviceStatus::Error);
            }
            emit errorOccurred(streamId, error);
        },
        Qt::QueuedConnection
    );
}

void MultiStreamPlaybackManager::publishMetrics()
{
    const QList<StreamMetrics> metrics = metricsSnapshot();
    for (const StreamMetrics &streamMetrics : metrics) {
        emit metricsUpdated(streamMetrics.streamId, streamMetrics);
    }
}
