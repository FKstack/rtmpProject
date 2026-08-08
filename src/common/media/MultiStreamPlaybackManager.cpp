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
    object.insert(QStringLiteral("submittedFrames"),
                  static_cast<qint64>(metrics.submittedFrames));
    object.insert(QStringLiteral("mailboxOverwrittenFrames"),
                  static_cast<qint64>(metrics.mailboxOverwrittenFrames));
    object.insert(QStringLiteral("unsupportedFrames"),
                  static_cast<qint64>(metrics.unsupportedFrames));
    object.insert(QStringLiteral("uploadedFrames"),
                  static_cast<qint64>(metrics.uploadedFrames));
    object.insert(QStringLiteral("renderedFrames"),
                  static_cast<qint64>(metrics.renderedFrames));
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
    object.insert(QStringLiteral("uploadCpuUs"), metrics.uploadCpuUs);
    object.insert(QStringLiteral("paintCpuUs"), metrics.paintCpuUs);
    object.insert(QStringLiteral("gpuTimeUs"), metrics.gpuTimeUs);
    object.insert(QStringLiteral("dirtyMerges"),
                  static_cast<qint64>(metrics.dirtyMerges));
    object.insert(QStringLiteral("scheduleChecks"),
                  static_cast<qint64>(metrics.scheduleChecks));
    object.insert(QStringLiteral("textureBytes"), metrics.textureBytes);
    return object;
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
    , metricsTimer_(std::make_unique<QTimer>())
    , uiWatchdogTimer_(std::make_unique<QTimer>())
{
    qRegisterMetaType<StreamMetrics>();
    qRegisterMetaType<PlaybackError>();

    metricsTimer_->setInterval(1'000);
    connect(
        metricsTimer_.get(),
        &QTimer::timeout,
        this,
        &MultiStreamPlaybackManager::publishMetrics
    );
    metricsTimer_->start();

    uiWatchdogTimer_->setTimerType(Qt::CoarseTimer);
    uiWatchdogTimer_->setInterval(33);
    connect(
        uiWatchdogTimer_.get(),
        &QTimer::timeout,
        this,
        &MultiStreamPlaybackManager::observeUiTimer
    );
    uiWatchdogTimer_->start();
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
    uiWatchdogTimer_->stop();
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

std::shared_ptr<LatestFrameMailbox>
MultiStreamPlaybackManager::frameMailbox(StreamId streamId) const
{
    const Entry *entry = entryFor(streamId);
    return entry != nullptr ? entry->player->frameMailbox() : nullptr;
}

QJsonObject rendererToJson(const RenderRuntimeMetrics &metrics)
{
    QJsonObject object;
    object.insert(QStringLiteral("requestedBackend"), metrics.requestedBackend);
    object.insert(QStringLiteral("activeBackend"), metrics.activeBackend);
    object.insert(QStringLiteral("fallbackOccurred"), metrics.fallbackOccurred);
    object.insert(QStringLiteral("fallbackReason"), metrics.fallbackReason);
    object.insert(QStringLiteral("graphicsApi"), metrics.graphicsApi);
    object.insert(QStringLiteral("vendor"), metrics.openGlVendor);
    object.insert(QStringLiteral("renderer"), metrics.openGlRenderer);
    object.insert(QStringLiteral("version"), metrics.openGlVersion);
    return object;
}

QJsonObject renderStatisticsToJson(const RenderRuntimeMetrics &metrics)
{
    QJsonObject object;
    object.insert(QStringLiteral("scheduleChecks"),
                  static_cast<qint64>(metrics.scheduleChecks));
    object.insert(QStringLiteral("updateRequests"),
                  static_cast<qint64>(metrics.updateRequests));
    object.insert(QStringLiteral("dirtyMerges"),
                  static_cast<qint64>(metrics.dirtyMerges));
    object.insert(QStringLiteral("paintCalls"),
                  static_cast<qint64>(metrics.paintCalls));
    object.insert(QStringLiteral("uploadedFrames"),
                  static_cast<qint64>(metrics.uploadedFrames));
    object.insert(QStringLiteral("renderedFrames"),
                  static_cast<qint64>(metrics.renderedFrames));
    object.insert(QStringLiteral("unsupportedFrames"),
                  static_cast<qint64>(metrics.unsupportedFrames));
    object.insert(QStringLiteral("paintCpuUs"), metrics.paintCpuUs);
    object.insert(QStringLiteral("uploadCpuUs"), metrics.uploadCpuUs);
    object.insert(QStringLiteral("gpuTimeUs"), metrics.gpuTimeUs);
    object.insert(QStringLiteral("latestFrameAgeMs"), metrics.latestFrameAgeMs);
    object.insert(QStringLiteral("textureBytes"), metrics.textureBytes);
    object.insert(QStringLiteral("renderItemCount"), metrics.renderItemCount);
    object.insert(QStringLiteral("visibleRenderItemCount"),
                  metrics.visibleRenderItemCount);
    object.insert(QStringLiteral("boundMailboxCount"),
                  metrics.boundMailboxCount);
    return object;
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

void MultiStreamPlaybackManager::setRenderMetricsProvider(
    std::function<RenderRuntimeMetrics()> provider
)
{
    renderMetricsProvider_ = std::move(provider);
}

void MultiStreamPlaybackManager::observeUiTimer()
{
    const qint64 now = monotonicMilliseconds();
    if (lastUiWatchdogTickMs_ > 0) {
        maximumUiTimerGapMs_ = std::max(
            maximumUiTimerGapMs_, now - lastUiWatchdogTickMs_
        );
    }
    lastUiWatchdogTickMs_ = now;
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
    root.insert(QStringLiteral("schemaVersion"), 3);
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
    const RenderRuntimeMetrics renderMetrics = renderMetricsProvider_
                                                   ? renderMetricsProvider_()
                                                   : RenderRuntimeMetrics {};
    root.insert(QStringLiteral("renderer"), rendererToJson(renderMetrics));
    root.insert(
        QStringLiteral("renderStatistics"),
        renderStatisticsToJson(renderMetrics)
    );
    root.insert(QStringLiteral("streams"), streams);

    QSaveFile file(metricsOutputPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}
