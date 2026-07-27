#include "media/MultiStreamPlaybackManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

constexpr int kMaximumStreams = 16;

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           )
        .count();
}
int resolveWorkerCount(const PlaybackPerformanceOptions &options)
{
    if (options.decodeWorkerCount > 0) {
        return std::clamp(options.decodeWorkerCount, 1, 16);
    }
    const int ideal = QThread::idealThreadCount();
    return std::clamp(ideal > 0 ? ideal / 2 : 1, 1, 8);
}

QJsonObject metricsToJson(const StreamMetrics &metrics)
{
    QJsonObject object;
    object.insert(QStringLiteral("streamId"),
                  QString::number(metrics.streamId));
    object.insert(QStringLiteral("displayName"), metrics.displayName);
    object.insert(QStringLiteral("state"), metrics.state);
    object.insert(QStringLiteral("packetsReceived"),
                  static_cast<qint64>(metrics.packetsReceived));
    object.insert(QStringLiteral("packetBytesReceived"),
                  static_cast<qint64>(metrics.packetBytesReceived));
    object.insert(QStringLiteral("packetsDropped"),
                  static_cast<qint64>(metrics.packetsDropped));
    object.insert(QStringLiteral("decodedFrames"),
                  static_cast<qint64>(metrics.decodedFrames));
    object.insert(QStringLiteral("convertedFrames"),
                  static_cast<qint64>(metrics.convertedFrames));
    object.insert(QStringLiteral("presentedFrames"),
                  static_cast<qint64>(metrics.presentedFrames));
    object.insert(QStringLiteral("reconnectCount"),
                  static_cast<qint64>(metrics.reconnectCount));
    object.insert(QStringLiteral("queuePackets"), metrics.queuePackets);
    object.insert(QStringLiteral("queueBytes"), metrics.queueBytes);
    object.insert(QStringLiteral("decodeFps"), metrics.decodeFps);
    object.insert(QStringLiteral("displayFps"), metrics.displayFps);
    object.insert(QStringLiteral("lastFrameAgeMs"), metrics.lastFrameAgeMs);
    object.insert(QStringLiteral("internalLatencyP95Ms"),
                  metrics.internalLatencyP95Ms);
    object.insert(QStringLiteral("sourceLatencyP50Ms"),
                  metrics.sourceLatencyP50Ms);
    object.insert(QStringLiteral("sourceLatencyP95Ms"),
                  metrics.sourceLatencyP95Ms);
    object.insert(QStringLiteral("sourceLatencyMaxMs"),
                  metrics.sourceLatencyMaxMs);
    object.insert(QStringLiteral("sourceLatencySamples"),
                  static_cast<qint64>(metrics.sourceLatencySamples));
    return object;
}

} // namespace

struct MultiStreamPlaybackManager::Entry
{
    StreamConnection connection;
    std::unique_ptr<FFmpegPlayer> player;
    std::uint64_t lastPresentedSequence = 0;
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
    , presentationTimer_(std::make_unique<QTimer>())
    , metricsTimer_(std::make_unique<QTimer>())
{
    qRegisterMetaType<StreamMetrics>();
    qRegisterMetaType<PresentableVideoFrame>();

    presentationTimer_->setTimerType(Qt::PreciseTimer);
    presentationTimer_->setInterval(33);
    connect(
        presentationTimer_.get(),
        &QTimer::timeout,
        this,
        &MultiStreamPlaybackManager::presentLatestFrames
    );
    presentationTimer_->start();

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
    presentationTimer_->stop();
    metricsTimer_->stop();
    stopAll();
    entries_.clear();
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
    entry->player->setObjectName(
        QStringLiteral("streamPlayer%1").arg(entry->connection.id)
    );
    entry->player->setAutomaticFrameSignalsEnabled(false);

    const StreamId streamId = entry->connection.id;
    connect(
        entry->player.get(),
        &FFmpegPlayer::stateChanged,
        this,
        [this, streamId](FFmpegPlayer::PlaybackState state) {
            emit stateChanged(streamId, state);
        }
    );
    connect(
        entry->player.get(),
        &FFmpegPlayer::errorOccurred,
        this,
        [this, streamId](const QString &message) {
            emit errorOccurred(streamId, message);
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
    entry->lastPresentedSequence = 0;
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
}

bool MultiStreamPlaybackManager::isStreamRunning(StreamId streamId) const noexcept
{
    Q_ASSERT(QThread::currentThread() == thread());
    const Entry *entry = entryFor(streamId);
    return entry != nullptr && entry->player->isRunning();
}

void MultiStreamPlaybackManager::setPresentationTarget(
    StreamId streamId,
    const PresentationTarget &target
)
{
    if (Entry *entry = entryFor(streamId); entry != nullptr) {
        entry->player->setPresentationTarget(target);
    }
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

void MultiStreamPlaybackManager::setMetricsOutputPath(const QString &path)
{
    metricsOutputPath_ = path.trimmed();
}

QString MultiStreamPlaybackManager::metricsOutputPath() const
{
    return metricsOutputPath_;
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

void MultiStreamPlaybackManager::presentLatestFrames()
{
    const qint64 now = monotonicMilliseconds();
    if (lastPresentationTickMs_ > 0) {
        maximumUiTimerGapMs_ = std::max(
            maximumUiTimerGapMs_, now - lastPresentationTickMs_
        );
    }
    lastPresentationTickMs_ = now;

    for (const auto &entry : entries_) {
        const PresentableVideoFrame frame = entry->player->latestFrame();
        if (frame.image.isNull() ||
            frame.sequence == entry->lastPresentedSequence) {
            continue;
        }
        entry->lastPresentedSequence = frame.sequence;
        entry->player->markFramePresented(frame);
        emit frameReady(entry->connection.id, frame);
    }
}

void MultiStreamPlaybackManager::publishMetrics()
{
    const QList<StreamMetrics> metrics = metricsSnapshot();
    for (const StreamMetrics &streamMetrics : metrics) {
        emit metricsUpdated(streamMetrics.streamId, streamMetrics);
    }
    if (!metricsOutputPath_.isEmpty()) {
        writeMetricsFile(metrics);
    }
}

void MultiStreamPlaybackManager::writeMetricsFile(
    const QList<StreamMetrics> &metrics
)
{
    const QFileInfo outputInfo(metricsOutputPath_);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        return;
    }

    QJsonArray streams;
    for (const StreamMetrics &streamMetrics : metrics) {
        streams.append(metricsToJson(streamMetrics));
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(
        QStringLiteral("generatedAtUtc"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
    );
    root.insert(
        QStringLiteral("processId"),
        static_cast<qint64>(QCoreApplication::applicationPid())
    );
    root.insert(QStringLiteral("decodeWorkerCount"), decodeWorkerCount());
    root.insert(QStringLiteral("streamCount"), streamCount());
    root.insert(QStringLiteral("maximumUiTimerGapMs"), maximumUiTimerGapMs_);
    root.insert(QStringLiteral("streams"), streams);

    QSaveFile file(metricsOutputPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}
