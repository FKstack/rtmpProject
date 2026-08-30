#include "diagnostics/RuntimeMetricsReporter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <utility>

#include "media/MultiStreamPlaybackManager.h"

namespace {

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           )
        .count();
}

QJsonObject streamMetricsToJson(const StreamMetrics &metrics)
{
    QJsonObject object;
    object.insert(QStringLiteral("streamId"), QString::number(metrics.streamId));
    object.insert(QStringLiteral("displayName"), metrics.displayName);
    object.insert(QStringLiteral("state"), metrics.state);
    object.insert(QStringLiteral("packetsReceived"), static_cast<qint64>(metrics.packetsReceived));
    object.insert(QStringLiteral("packetBytesReceived"), static_cast<qint64>(metrics.packetBytesReceived));
    object.insert(QStringLiteral("packetsDropped"), static_cast<qint64>(metrics.packetsDropped));
    object.insert(QStringLiteral("decodedFrames"), static_cast<qint64>(metrics.decodedFrames));
    object.insert(QStringLiteral("convertedFrames"), static_cast<qint64>(metrics.convertedFrames));
    object.insert(QStringLiteral("presentedFrames"), static_cast<qint64>(metrics.presentedFrames));
    object.insert(QStringLiteral("submittedFrames"), static_cast<qint64>(metrics.submittedFrames));
    object.insert(QStringLiteral("mailboxOverwrittenFrames"), static_cast<qint64>(metrics.mailboxOverwrittenFrames));
    object.insert(QStringLiteral("unsupportedFrames"), static_cast<qint64>(metrics.unsupportedFrames));
    object.insert(QStringLiteral("markerDecodedFrames"), static_cast<qint64>(metrics.markerDecodedFrames));
    object.insert(QStringLiteral("markerDecodeFailures"), static_cast<qint64>(metrics.markerDecodeFailures));
    object.insert(QStringLiteral("sourceSequenceGaps"), static_cast<qint64>(metrics.sourceSequenceGaps));
    object.insert(QStringLiteral("uploadedFrames"), static_cast<qint64>(metrics.uploadedFrames));
    object.insert(QStringLiteral("renderedFrames"), static_cast<qint64>(metrics.renderedFrames));
    object.insert(QStringLiteral("reconnectCount"), static_cast<qint64>(metrics.reconnectCount));
    object.insert(QStringLiteral("queuePackets"), metrics.queuePackets);
    object.insert(QStringLiteral("queueBytes"), metrics.queueBytes);
    object.insert(QStringLiteral("decodeFps"), metrics.decodeFps);
    object.insert(QStringLiteral("displayFps"), metrics.displayFps);
    object.insert(QStringLiteral("achievedDisplayFps"), metrics.displayFps);
    object.insert(QStringLiteral("lastFrameAgeMs"), metrics.lastFrameAgeMs);
    object.insert(QStringLiteral("internalLatencyP50Ms"), metrics.internalLatencyP50Ms);
    object.insert(QStringLiteral("internalLatencyP95Ms"), metrics.internalLatencyP95Ms);
    object.insert(QStringLiteral("internalLatencyMaxMs"), metrics.internalLatencyMaxMs);
    object.insert(QStringLiteral("sourceLatencyP50Ms"), metrics.sourceLatencyP50Ms);
    object.insert(QStringLiteral("sourceLatencyP95Ms"), metrics.sourceLatencyP95Ms);
    object.insert(QStringLiteral("sourceLatencyMaxMs"), metrics.sourceLatencyMaxMs);
    object.insert(QStringLiteral("sourceLatencySamples"), static_cast<qint64>(metrics.sourceLatencySamples));
    object.insert(QStringLiteral("presentationIntervalP50Ms"), metrics.presentationIntervalP50Ms);
    object.insert(QStringLiteral("presentationIntervalP95Ms"), metrics.presentationIntervalP95Ms);
    object.insert(QStringLiteral("presentationIntervalMaxMs"), metrics.presentationIntervalMaxMs);
    object.insert(QStringLiteral("lastPresentedSourceSequence"), metrics.lastPresentedSourceSequence);
    object.insert(QStringLiteral("uploadCpuUs"), metrics.uploadCpuUs);
    object.insert(QStringLiteral("paintCpuUs"), metrics.paintCpuUs);
    object.insert(QStringLiteral("gpuTimeUs"), metrics.gpuTimeUs);
    object.insert(QStringLiteral("dirtyMerges"), static_cast<qint64>(metrics.dirtyMerges));
    object.insert(QStringLiteral("scheduleChecks"), static_cast<qint64>(metrics.scheduleChecks));
    object.insert(QStringLiteral("textureBytes"), metrics.textureBytes);
    return object;
}

QString audioStateName(AudioPlaybackState state)
{
    switch (state) {
    case AudioPlaybackState::Unavailable:
        return QStringLiteral("unavailable");
    case AudioPlaybackState::Buffering:
        return QStringLiteral("buffering");
    case AudioPlaybackState::Playing:
        return QStringLiteral("playing");
    case AudioPlaybackState::Muted:
        return QStringLiteral("muted");
    case AudioPlaybackState::OutputError:
        return QStringLiteral("outputError");
    }
    return QStringLiteral("unavailable");
}

QJsonObject audioMetricsToJson(const AudioPlaybackMetrics &metrics)
{
    return {
        {QStringLiteral("streamId"), QString::number(metrics.streamId)},
        {QStringLiteral("state"), audioStateName(metrics.state)},
        {QStringLiteral("packetsReceived"), static_cast<qint64>(metrics.packetsReceived)},
        {QStringLiteral("decodedPackets"), static_cast<qint64>(metrics.decodedPackets)},
        {QStringLiteral("packetsDropped"), static_cast<qint64>(metrics.packetsDropped)},
        {QStringLiteral("underruns"), static_cast<qint64>(metrics.underruns)},
        {QStringLiteral("underrunDurationMs"), metrics.underrunDurationMs},
        {QStringLiteral("queuedPackets"), metrics.queuedPackets},
        {QStringLiteral("queuedBytes"), metrics.queuedBytes},
        {QStringLiteral("pcmBufferedMs"), metrics.pcmBufferedMs},
        {QStringLiteral("requestedSinkBufferMs"), metrics.requestedSinkBufferMs},
        {QStringLiteral("actualSinkBufferMs"), metrics.actualSinkBufferMs},
        {QStringLiteral("outputLatencyP50Ms"), metrics.outputLatencyP50Ms},
        {QStringLiteral("outputLatencyP95Ms"), metrics.outputLatencyP95Ms}
    };
}

QJsonObject rendererToJson(const RenderRuntimeMetrics &metrics)
{
    return {
        {QStringLiteral("requestedBackend"), metrics.requestedBackend},
        {QStringLiteral("activeBackend"), metrics.activeBackend},
        {QStringLiteral("fallbackOccurred"), metrics.fallbackOccurred},
        {QStringLiteral("fallbackReason"), metrics.fallbackReason},
        {QStringLiteral("requestedDisplayFps"), metrics.requestedDisplayFps},
        {QStringLiteral("effectiveDisplayFps"), metrics.effectiveDisplayFps},
        {QStringLiteral("graphicsApi"), metrics.graphicsApi},
        {QStringLiteral("vendor"), metrics.openGlVendor},
        {QStringLiteral("renderer"), metrics.openGlRenderer},
        {QStringLiteral("version"), metrics.openGlVersion}
    };
}

QJsonObject renderStatisticsToJson(const RenderRuntimeMetrics &metrics)
{
    return {
        {QStringLiteral("scheduleChecks"), static_cast<qint64>(metrics.scheduleChecks)},
        {QStringLiteral("deadlineMisses"), static_cast<qint64>(metrics.deadlineMisses)},
        {QStringLiteral("updateRequests"), static_cast<qint64>(metrics.updateRequests)},
        {QStringLiteral("dirtyMerges"), static_cast<qint64>(metrics.dirtyMerges)},
        {QStringLiteral("paintCalls"), static_cast<qint64>(metrics.paintCalls)},
        {QStringLiteral("uploadedFrames"), static_cast<qint64>(metrics.uploadedFrames)},
        {QStringLiteral("renderedFrames"), static_cast<qint64>(metrics.renderedFrames)},
        {QStringLiteral("unsupportedFrames"), static_cast<qint64>(metrics.unsupportedFrames)},
        {QStringLiteral("paintCpuUs"), metrics.paintCpuUs},
        {QStringLiteral("uploadCpuUs"), metrics.uploadCpuUs},
        {QStringLiteral("gpuTimeUs"), metrics.gpuTimeUs},
        {QStringLiteral("paintCpuP95Us"), metrics.paintCpuP95Us},
        {QStringLiteral("uploadCpuP95Us"), metrics.uploadCpuP95Us},
        {QStringLiteral("gpuTimeP95Us"), metrics.gpuTimeP95Us},
        {QStringLiteral("latestFrameAgeMs"), metrics.latestFrameAgeMs},
        {QStringLiteral("textureBytes"), metrics.textureBytes},
        {QStringLiteral("renderItemCount"), metrics.renderItemCount},
        {QStringLiteral("visibleRenderItemCount"), metrics.visibleRenderItemCount},
        {QStringLiteral("boundMailboxCount"), metrics.boundMailboxCount}
    };
}

} // namespace

RuntimeMetricsReporter::RuntimeMetricsReporter(
    MultiStreamPlaybackManager *playbackManager,
    QObject *parent
)
    : QObject(parent)
    , playbackManager_(playbackManager)
    , metricsTimer_(std::make_unique<QTimer>())
    , uiWatchdogTimer_(std::make_unique<QTimer>())
{
    Q_ASSERT(playbackManager_ != nullptr);
    metricsTimer_->setInterval(1'000);
    connect(metricsTimer_.get(), &QTimer::timeout,
            this, &RuntimeMetricsReporter::writeSnapshot);
    metricsTimer_->start();

    uiWatchdogTimer_->setTimerType(Qt::CoarseTimer);
    uiWatchdogTimer_->setInterval(33);
    connect(uiWatchdogTimer_.get(), &QTimer::timeout,
            this, &RuntimeMetricsReporter::observeUiTimer);
    uiWatchdogTimer_->start();
}

RuntimeMetricsReporter::~RuntimeMetricsReporter() = default;

void RuntimeMetricsReporter::setOutputPath(const QString &path)
{
    outputPath_ = path.trimmed();
}

QString RuntimeMetricsReporter::outputPath() const
{
    return outputPath_;
}

void RuntimeMetricsReporter::setRenderMetricsProvider(
    std::function<RenderRuntimeMetrics()> provider
)
{
    renderMetricsProvider_ = std::move(provider);
}

bool RuntimeMetricsReporter::writeSnapshot()
{
    if (playbackManager_ == nullptr || outputPath_.isEmpty()) {
        return false;
    }
    const QFileInfo outputInfo(outputPath_);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        return false;
    }

    const QList<StreamMetrics> metrics = playbackManager_->metricsSnapshot();
    QJsonArray streams;
    QJsonArray achievedDisplayFps;
    for (const StreamMetrics &streamMetrics : metrics) {
        streams.append(streamMetricsToJson(streamMetrics));
        achievedDisplayFps.append(QJsonObject {
            {QStringLiteral("streamId"), QString::number(streamMetrics.streamId)},
            {QStringLiteral("fps"), streamMetrics.displayFps}
        });
    }
    const RenderRuntimeMetrics renderMetrics = renderMetricsProvider_
                                                   ? renderMetricsProvider_()
                                                   : RenderRuntimeMetrics {};
    const AudioPlaybackMetrics audioMetrics = playbackManager_->audioMetrics();
    QJsonObject root {
        {QStringLiteral("schemaVersion"), 4},
        {QStringLiteral("generatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("processId"), static_cast<qint64>(QCoreApplication::applicationPid())},
        {QStringLiteral("decodeWorkerCount"), playbackManager_->decodeWorkerCount()},
        {QStringLiteral("streamCount"), playbackManager_->streamCount()},
        {QStringLiteral("maximumUiTimerGapMs"), maximumUiTimerGapMs_},
        {QStringLiteral("renderer"), rendererToJson(renderMetrics)},
        {QStringLiteral("displayFrameRate"), QJsonObject {
            {QStringLiteral("requested"), renderMetrics.requestedDisplayFps},
            {QStringLiteral("effective"), renderMetrics.effectiveDisplayFps},
            {QStringLiteral("fallbackOccurred"), renderMetrics.fallbackOccurred},
            {QStringLiteral("fallbackReason"), renderMetrics.fallbackReason},
            {QStringLiteral("achievedByStream"), achievedDisplayFps}
        }},
        {QStringLiteral("renderStatistics"), renderStatisticsToJson(renderMetrics)},
        {QStringLiteral("audio"), audioMetricsToJson(audioMetrics)},
        {QStringLiteral("streams"), streams}
    };

    QSaveFile file(outputPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

qint64 RuntimeMetricsReporter::maximumUiTimerGapMs() const noexcept
{
    return maximumUiTimerGapMs_;
}

void RuntimeMetricsReporter::observeUiTimer()
{
    const qint64 now = monotonicMilliseconds();
    if (lastUiWatchdogTickMs_ > 0) {
        maximumUiTimerGapMs_ = std::max(
            maximumUiTimerGapMs_, now - lastUiWatchdogTickMs_
        );
    }
    lastUiWatchdogTickMs_ = now;
}
