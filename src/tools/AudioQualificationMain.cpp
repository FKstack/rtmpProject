#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "media/AudioPlaybackEngine.h"
#include "media/DecodeWorkerPool.h"
#include "media/FFmpegPlayer.h"
#include "media/LatestFrameMailbox.h"

namespace {

qint64 steadyMicroseconds()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           ).count();
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return -1.0;
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    if (lower == upper) return values[lower];
    const double weight = position - static_cast<double>(lower);
    return values[lower] + (values[upper] - values[lower]) * weight;
}

class SampleCollector final : public AudioPlaybackObserver
{
public:
    void onAudioPlaybackSample(
        const AudioPlaybackProbeSample &sample
    ) noexcept override
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        samples_.push_back(sample);
    }

    std::vector<AudioPlaybackProbeSample> snapshot() const
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        return samples_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<AudioPlaybackProbeSample> samples_;
};

struct ProgressPoint
{
    qint64 mediaPtsUs = -1;
    qint64 observedMonotonicUs = -1;
};

class QualificationController final : public QObject
{
public:
    QualificationController(
        QString ffmpeg,
        QString input,
        QString url,
        QString output,
        int durationSeconds,
        int warmupSeconds,
        int minimumSamples,
        bool silentDiagnostic,
        QObject *parent = nullptr
    )
        : QObject(parent)
        , ffmpeg_(std::move(ffmpeg))
        , input_(std::move(input))
        , url_(std::move(url))
        , output_(std::move(output))
        , durationSeconds_(durationSeconds)
        , warmupSeconds_(warmupSeconds)
        , minimumSamples_(minimumSamples)
        , silentDiagnostic_(silentDiagnostic)
        , decodePool_(1)
        , player_(
              1,
              QStringLiteral("MP4 qualification"),
              &decodePool_,
              PlaybackPerformanceOptions {}
          )
        , collector_(std::make_shared<SampleCollector>())
    {
        publisher_.setProcessChannelMode(QProcess::SeparateChannels);
        connect(
            &publisher_, &QProcess::readyReadStandardOutput,
            this, [this] { consumePublisherProgress(); }
        );
        connect(
            &publisher_, &QProcess::readyReadStandardError,
            this, [this] { consumePublisherDiagnostics(); }
        );
        connect(
            &publisher_,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus) {
                if (!finishing_ && exitCode != 0) {
                    failure_ = QStringLiteral("FFmpeg publisher exited with code %1")
                                   .arg(exitCode);
                    finish();
                }
            }
        );
        connect(
            &player_, &FFmpegPlayer::errorOccurred,
            this, [this](const PlaybackError &error) {
                lastPlayerError_ = error.technicalMessage;
            }
        );
        connect(
            &engine_, &AudioPlaybackEngine::stateChanged,
            this, [this](StreamId streamId, AudioPlaybackState state) {
                if (streamId != 1) return;
                const qint64 now = steadyMicroseconds();
                if (state == AudioPlaybackState::Muted && muteRequestedUs_ > 0 &&
                    muteObservedUs_ < 0) {
                    muteObservedUs_ = now;
                }
                if (state == AudioPlaybackState::Playing &&
                    unmuteRequestedUs_ > 0 && unmuteObservedUs_ < 0) {
                    unmuteObservedUs_ = now;
                }
            }
        );
        renderTimer_.setInterval(33);
        connect(&renderTimer_, &QTimer::timeout, this, [this] {
            const auto mailbox = player_.frameMailbox();
            if (mailbox == nullptr) return;
            const std::uint64_t sequence = mailbox->latestSequence();
            if (sequence == 0 || sequence == lastRenderedSequence_) return;
            lastRenderedSequence_ = sequence;
            mailbox->recordRendered();
        });
    }

    void start()
    {
        const QStringList arguments {
            QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
            QStringLiteral("-loglevel"), QStringLiteral("warning"),
            QStringLiteral("-re"), QStringLiteral("-fflags"),
            QStringLiteral("+genpts"), QStringLiteral("-i"), input_,
            // The approved MP4 stores roughly 400 ms of video packets followed
            // by 400 ms of audio packets. A single -re demuxer therefore feeds
            // the encoders in audible bursts. Independent real-time demuxers
            // preserve the same file/content while pacing each selected track
            // by its own timestamps.
            QStringLiteral("-re"), QStringLiteral("-fflags"),
            QStringLiteral("+genpts"), QStringLiteral("-i"), input_,
            QStringLiteral("-map"), QStringLiteral("0:v:0"),
            QStringLiteral("-map"), QStringLiteral("1:a:0"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-preset"), QStringLiteral("ultrafast"),
            QStringLiteral("-tune"), QStringLiteral("zerolatency"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-r"), QStringLiteral("25"),
            QStringLiteral("-g"), QStringLiteral("25"),
            QStringLiteral("-keyint_min"), QStringLiteral("25"),
            QStringLiteral("-bf"), QStringLiteral("0"),
            QStringLiteral("-sc_threshold"), QStringLiteral("0"),
            QStringLiteral("-c:a"), QStringLiteral("aac"),
            QStringLiteral("-profile:a"), QStringLiteral("aac_low"),
            QStringLiteral("-ar"), QStringLiteral("48000"),
            QStringLiteral("-ac"), QStringLiteral("1"),
            QStringLiteral("-b:a"), QStringLiteral("64k"),
            QStringLiteral("-flags"), QStringLiteral("+global_header+low_delay"),
            QStringLiteral("-flvflags"), QStringLiteral("no_duration_filesize"),
            QStringLiteral("-flush_packets"), QStringLiteral("1"),
            QStringLiteral("-progress"), QStringLiteral("pipe:1"),
            QStringLiteral("-stats_period"), QStringLiteral("0.01"),
            QStringLiteral("-rtmp_buffer"), QStringLiteral("0"),
            QStringLiteral("-tcp_nodelay"), QStringLiteral("1"),
            QStringLiteral("-f"), QStringLiteral("flv"), url_
        };
        publisher_.start(ffmpeg_, arguments, QIODevice::ReadOnly);
        if (!publisher_.waitForStarted(10'000)) {
            failure_ = QStringLiteral("Unable to start FFmpeg publisher: %1")
                           .arg(publisher_.errorString());
            QTimer::singleShot(0, this, [this] { finish(); });
            return;
        }
        QTimer::singleShot(1'500, this, [this] { startClient(); });
    }

    int exitCode() const noexcept { return exitCode_; }

private:
    void startClient()
    {
        clientStartedUs_ = steadyMicroseconds();
        engine_.setQualificationObserver(collector_);
        engine_.setVideoClockSource(1, player_.frameMailbox());
        player_.setAudioPacketSink(&engine_);
        engine_.selectStream(1);
        engine_.setMuted(silentDiagnostic_);
        if (!player_.start(url_)) {
            failure_ = QStringLiteral("Client player refused to start");
            finish();
            return;
        }
        renderTimer_.start();
        if (!silentDiagnostic_) {
            const int muteAtMs = std::max(5, warmupSeconds_ + 5) * 1000;
            QTimer::singleShot(muteAtMs, this, [this] {
                muteRequestedUs_ = steadyMicroseconds();
                engine_.setMuted(true);
                QTimer::singleShot(500, this, [this] {
                    unmuteRequestedUs_ = steadyMicroseconds();
                    engine_.setMuted(false);
                });
            });
        }
        QTimer::singleShot(
            durationSeconds_ * 1000, this, [this] { finish(); }
        );
    }

    void consumePublisherProgress()
    {
        progressBuffer_.append(publisher_.readAllStandardOutput());
        for (;;) {
            const qsizetype newline = progressBuffer_.indexOf('\n');
            if (newline < 0) break;
            const QByteArray line = progressBuffer_.left(newline).trimmed();
            progressBuffer_.remove(0, newline + 1);
            const qsizetype equals = line.indexOf('=');
            if (equals < 0) continue;
            const QByteArray key = line.left(equals);
            const QByteArray value = line.mid(equals + 1);
            if (key == "out_time_us" || key == "out_time_ms") {
                bool ok = false;
                const qint64 parsed = value.toLongLong(&ok);
                if (ok) currentPublisherPtsUs_ = parsed;
            } else if (key == "progress" && currentPublisherPtsUs_ >= 0) {
                progress_.push_back(
                    {currentPublisherPtsUs_, steadyMicroseconds()}
                );
            }
        }
    }

    void consumePublisherDiagnostics()
    {
        diagnosticBuffer_.append(publisher_.readAllStandardError());
        for (;;) {
            const qsizetype newline = diagnosticBuffer_.indexOf('\n');
            if (newline < 0) break;
            const QByteArray line = diagnosticBuffer_.left(newline).trimmed();
            diagnosticBuffer_.remove(0, newline + 1);
            if (line.contains("[aost#0:1/") && line.contains("muxer <-")) {
                const qsizetype marker = line.indexOf("pts_time:");
                if (marker >= 0) {
                    const qsizetype valueStart = marker + 9;
                    qsizetype valueEnd = line.indexOf(' ', valueStart);
                    if (valueEnd < 0) valueEnd = line.size();
                    bool ok = false;
                    const double seconds = line.mid(
                        valueStart, valueEnd - valueStart
                    ).toDouble(&ok);
                    if (ok) {
                        audioMuxProgress_.push_back({
                            static_cast<qint64>(std::llround(seconds * 1'000'000.0)),
                            steadyMicroseconds()
                        });
                    }
                }
            }
            publisherError_.append(line);
            publisherError_.append('\n');
            if (publisherError_.size() > 20'000) {
                publisherError_.remove(0, publisherError_.size() - 20'000);
            }
        }
    }

    std::optional<qint64> sourceTimeForPts(qint64 mediaPtsUs) const
    {
        const std::vector<ProgressPoint> &timeline =
            audioMuxProgress_.size() >= 2 ? audioMuxProgress_ : progress_;
        if (timeline.size() < 2 || mediaPtsUs < 0) return std::nullopt;
        const auto upper = std::lower_bound(
            timeline.begin(), timeline.end(), mediaPtsUs,
            [](const ProgressPoint &point, qint64 pts) {
                return point.mediaPtsUs < pts;
            }
        );
        if (upper == timeline.begin()) {
            return upper->observedMonotonicUs -
                   (upper->mediaPtsUs - mediaPtsUs);
        }
        if (upper == timeline.end()) {
            const ProgressPoint &last = timeline.back();
            return last.observedMonotonicUs + (mediaPtsUs - last.mediaPtsUs);
        }
        const ProgressPoint &after = *upper;
        const ProgressPoint &before = *(upper - 1);
        const qint64 mediaSpan = after.mediaPtsUs - before.mediaPtsUs;
        if (mediaSpan <= 0) return before.observedMonotonicUs;
        const double fraction = static_cast<double>(mediaPtsUs - before.mediaPtsUs) /
                                static_cast<double>(mediaSpan);
        return before.observedMonotonicUs + static_cast<qint64>(
            fraction * static_cast<double>(
                after.observedMonotonicUs - before.observedMonotonicUs
            )
        );
    }

    static QJsonValue metricValue(double value)
    {
        return value >= 0.0 ? QJsonValue(std::round(value * 1000.0) / 1000.0)
                            : QJsonValue(QJsonValue::Null);
    }

    static QJsonValue signedMetricValue(double value)
    {
        return std::abs(value) < 999'999.0
            ? QJsonValue(std::round(value * 1000.0) / 1000.0)
            : QJsonValue(QJsonValue::Null);
    }

    QJsonObject buildReport()
    {
        const auto raw = collector_->snapshot();
        std::vector<AudioPlaybackProbeSample> samples;
        std::vector<double> latenciesMs;
        std::vector<double> receiveToDecodeMs;
        std::vector<double> publisherToReceiveMs;
        std::vector<double> decodeToQueueMs;
        std::vector<double> queueToSinkMs;
        std::vector<double> avSkewMs;
        qint64 previousSelectedUs = -1;
        qint64 previousSinkUs = -1;
        qint64 maximumGapUs = 0;
        const qint64 warmupBoundary = clientStartedUs_ +
                                      static_cast<qint64>(warmupSeconds_) * 1'000'000;
        for (const auto &sample : raw) {
            if (sample.sinkWriteMonotonicUs < warmupBoundary ||
                sample.mediaPtsUs < 0) continue;
            if (previousSinkUs >= 0) {
                const qint64 gap = sample.sinkWriteMonotonicUs - previousSinkUs;
                maximumGapUs = std::max(maximumGapUs, gap);
            }
            previousSinkUs = sample.sinkWriteMonotonicUs;
            if (previousSelectedUs >= 0 &&
                sample.sinkWriteMonotonicUs - previousSelectedUs < 900'000) {
                continue;
            }
            const auto sourceUs = sourceTimeForPts(sample.mediaPtsUs);
            if (!sourceUs.has_value()) continue;
            const qint64 latencyUs = sample.sinkWriteMonotonicUs - *sourceUs;
            if (latencyUs < 0 || latencyUs > 5'000'000) continue;
            previousSelectedUs = sample.sinkWriteMonotonicUs;
            samples.push_back(sample);
            latenciesMs.push_back(static_cast<double>(latencyUs) / 1000.0);
            publisherToReceiveMs.push_back(
                static_cast<double>(sample.packetReceivedMonotonicUs -
                                    *sourceUs) / 1000.0
            );
            receiveToDecodeMs.push_back(
                static_cast<double>(sample.decodedMonotonicUs -
                                    sample.packetReceivedMonotonicUs) / 1000.0
            );
            decodeToQueueMs.push_back(
                static_cast<double>(sample.queuedMonotonicUs -
                                    sample.decodedMonotonicUs) / 1000.0
            );
            queueToSinkMs.push_back(
                static_cast<double>(sample.sinkWriteMonotonicUs -
                                    sample.queuedMonotonicUs) / 1000.0
            );
            if (sample.videoRenderedPtsUs >= 0) {
                avSkewMs.push_back(
                    static_cast<double>(sample.mediaPtsUs -
                                        sample.videoRenderedPtsUs) / 1000.0
                );
            }
        }

        const double p50 = percentile(latenciesMs, 0.50);
        const double p95 = percentile(latenciesMs, 0.95);
        const double maximum = latenciesMs.empty()
            ? -1.0 : *std::max_element(latenciesMs.begin(), latenciesMs.end());
        const double startupMs = raw.empty() || clientStartedUs_ <= 0
            ? -1.0
            : static_cast<double>(raw.front().sinkWriteMonotonicUs - clientStartedUs_) /
                  1000.0;
        const double muteMs = muteObservedUs_ >= muteRequestedUs_ && muteRequestedUs_ > 0
            ? static_cast<double>(muteObservedUs_ - muteRequestedUs_) / 1000.0 : -1.0;
        const double unmuteMs = unmuteObservedUs_ >= unmuteRequestedUs_ && unmuteRequestedUs_ > 0
            ? static_cast<double>(unmuteObservedUs_ - unmuteRequestedUs_) / 1000.0 : -1.0;
        const double minimumSkew = avSkewMs.empty()
            ? -1'000'000.0 : *std::min_element(avSkewMs.begin(), avSkewMs.end());
        const double maximumSkew = avSkewMs.empty()
            ? 1'000'000.0 : *std::max_element(avSkewMs.begin(), avSkewMs.end());
        const qint64 observationSpanUs = raw.size() > 1
            ? raw.back().sinkWriteMonotonicUs - raw.front().sinkWriteMonotonicUs : 0;
        const AudioPlaybackMetrics engineMetrics = engine_.metricsSnapshot();
        const double underrunPercent = observationSpanUs > 0
            ? 100.0 * static_cast<double>(engineMetrics.underrunDurationMs) *
                  1000.0 / static_cast<double>(observationSpanUs)
            : 100.0;

        const bool passed = failure_.isEmpty() &&
            static_cast<int>(latenciesMs.size()) >= minimumSamples_ &&
            p50 >= 0.0 && p50 <= 100.0 && p95 <= 150.0 && maximum <= 250.0 &&
            startupMs >= 0.0 && startupMs <= 300.0 &&
            (silentDiagnostic_ || (muteMs >= 0.0 && muteMs <= 100.0)) &&
            (silentDiagnostic_ || (unmuteMs >= 0.0 && unmuteMs <= 150.0)) &&
            underrunPercent <= 0.5 && maximumGapUs <= 100'000 &&
            !avSkewMs.empty() && minimumSkew >= -125.0 && maximumSkew <= 45.0;

        QJsonObject report {
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("measurementScope"),
             QStringLiteral("ffmpeg-publisher-progress-to-qaudiosink-write")},
            {QStringLiteral("acousticOutputIncluded"), false},
            {QStringLiteral("silentDiagnostic"), silentDiagnostic_},
            {QStringLiteral("durationSeconds"), durationSeconds_},
            {QStringLiteral("warmupSeconds"), warmupSeconds_},
            {QStringLiteral("sampleCount"), static_cast<int>(latenciesMs.size())},
            {QStringLiteral("p50Ms"), metricValue(p50)},
            {QStringLiteral("p95Ms"), metricValue(p95)},
            {QStringLiteral("maximumMs"), metricValue(maximum)},
            {QStringLiteral("startupMs"), metricValue(startupMs)},
            {QStringLiteral("muteMs"), metricValue(muteMs)},
            {QStringLiteral("unmuteMs"), metricValue(unmuteMs)},
            {QStringLiteral("maximumSinkGapMs"), metricValue(
                 static_cast<double>(maximumGapUs) / 1000.0)},
            {QStringLiteral("underrunPercent"), metricValue(underrunPercent)},
            {QStringLiteral("avSkewMinimumMs"), signedMetricValue(minimumSkew)},
            {QStringLiteral("avSkewMaximumMs"), signedMetricValue(maximumSkew)},
            {QStringLiteral("receiveToDecodeP95Ms"),
             metricValue(percentile(receiveToDecodeMs, 0.95))},
            {QStringLiteral("publisherToReceiveP50Ms"),
             metricValue(percentile(publisherToReceiveMs, 0.50))},
            {QStringLiteral("publisherToReceiveP95Ms"),
             metricValue(percentile(publisherToReceiveMs, 0.95))},
            {QStringLiteral("decodeToQueueP95Ms"),
             metricValue(percentile(decodeToQueueMs, 0.95))},
            {QStringLiteral("queueToSinkP95Ms"),
             metricValue(percentile(queueToSinkMs, 0.95))},
            {QStringLiteral("requestedSinkBufferMs"), engineMetrics.requestedSinkBufferMs},
            {QStringLiteral("actualSinkBufferMs"), engineMetrics.actualSinkBufferMs},
            {QStringLiteral("packetsReceived"),
             static_cast<qint64>(engineMetrics.packetsReceived)},
            {QStringLiteral("packetsDropped"),
             static_cast<qint64>(engineMetrics.packetsDropped)},
            {QStringLiteral("underrunEvents"),
             static_cast<qint64>(engineMetrics.underruns)},
            {QStringLiteral("underrunDurationMs"),
             engineMetrics.underrunDurationMs},
            {QStringLiteral("progressSampleCount"),
             static_cast<int>(progress_.size())},
            {QStringLiteral("audioMuxSampleCount"),
             static_cast<int>(audioMuxProgress_.size())},
            {QStringLiteral("passed"), passed}
        };
        if (!failure_.isEmpty()) report.insert(QStringLiteral("failure"), failure_);
        if (!lastPlayerError_.isEmpty()) {
            report.insert(QStringLiteral("lastPlayerError"), lastPlayerError_);
        }
        if (!publisherError_.isEmpty()) {
            report.insert(
                QStringLiteral("publisherLogTail"),
                QString::fromLocal8Bit(publisherError_.right(2'000))
            );
        }
        exitCode_ = passed ? 0 : 1;
        return report;
    }

    void finish()
    {
        if (finishing_) return;
        finishing_ = true;
        renderTimer_.stop();
        player_.requestStop();
        player_.stop();
        engine_.setQualificationObserver({});
        engine_.stop();
        decodePool_.stop();
        if (publisher_.state() != QProcess::NotRunning) {
            publisher_.terminate();
            if (!publisher_.waitForFinished(5'000)) {
                publisher_.kill();
                publisher_.waitForFinished(2'000);
            }
        }
        consumePublisherProgress();
        consumePublisherDiagnostics();
        const QJsonObject report = buildReport();
        QFile output(output_);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            exitCode_ = 2;
        } else {
            output.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
            output.close();
        }
        qInfo().noquote() << QJsonDocument(report).toJson(QJsonDocument::Compact);
        QCoreApplication::exit(exitCode_);
    }

    QString ffmpeg_;
    QString input_;
    QString url_;
    QString output_;
    int durationSeconds_ = 320;
    int warmupSeconds_ = 10;
    int minimumSamples_ = 300;
    bool silentDiagnostic_ = false;
    QProcess publisher_;
    QByteArray progressBuffer_;
    QByteArray publisherError_;
    QByteArray diagnosticBuffer_;
    qint64 currentPublisherPtsUs_ = -1;
    std::vector<ProgressPoint> progress_;
    std::vector<ProgressPoint> audioMuxProgress_;
    DecodeWorkerPool decodePool_;
    FFmpegPlayer player_;
    AudioPlaybackEngine engine_;
    std::shared_ptr<SampleCollector> collector_;
    QTimer renderTimer_;
    std::uint64_t lastRenderedSequence_ = 0;
    qint64 clientStartedUs_ = -1;
    qint64 muteRequestedUs_ = -1;
    qint64 muteObservedUs_ = -1;
    qint64 unmuteRequestedUs_ = -1;
    qint64 unmuteObservedUs_ = -1;
    QString failure_;
    QString lastPlayerError_;
    bool finishing_ = false;
    int exitCode_ = 2;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rtmp_monitor_audio_qualification"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("ffmpeg")}, QStringLiteral("FFmpeg executable."), QStringLiteral("path")},
        {{QStringLiteral("input")}, QStringLiteral("MP4 input file."), QStringLiteral("path")},
        {{QStringLiteral("url")}, QStringLiteral("RTMP publish/play URL."), QStringLiteral("url")},
        {{QStringLiteral("output")}, QStringLiteral("Qualification JSON report."), QStringLiteral("path")},
        {{QStringLiteral("duration")}, QStringLiteral("Sampling duration in seconds."), QStringLiteral("seconds"), QStringLiteral("320")},
        {{QStringLiteral("warmup")}, QStringLiteral("Warm-up duration in seconds."), QStringLiteral("seconds"), QStringLiteral("10")},
        {{QStringLiteral("minimum-samples")}, QStringLiteral("Minimum gate sample count."), QStringLiteral("count"), QStringLiteral("300")},
        {{QStringLiteral("silent")}, QStringLiteral("Keep QAudioSink volume muted for diagnostic runs.")}
    });
    parser.process(application);

    bool durationOk = false;
    bool warmupOk = false;
    bool samplesOk = false;
    const int duration = parser.value(QStringLiteral("duration")).toInt(&durationOk);
    const int warmup = parser.value(QStringLiteral("warmup")).toInt(&warmupOk);
    const int minimumSamples = parser.value(QStringLiteral("minimum-samples")).toInt(&samplesOk);
    const bool silentDiagnostic = parser.isSet(QStringLiteral("silent"));
    const QString ffmpeg = parser.value(QStringLiteral("ffmpeg"));
    const QString input = parser.value(QStringLiteral("input"));
    const QString url = parser.value(QStringLiteral("url"));
    const QString output = parser.value(QStringLiteral("output"));
    if (ffmpeg.isEmpty() || input.isEmpty() || url.isEmpty() || output.isEmpty() ||
        !durationOk || !warmupOk || !samplesOk || duration <= warmup ||
        warmup < 0 || minimumSamples <= 0) {
        parser.showHelp(2);
    }

    QualificationController controller(
        ffmpeg, input, url, output, duration, warmup, minimumSamples,
        silentDiagnostic
    );
    QTimer::singleShot(0, &controller, [&controller] { controller.start(); });
    application.exec();
    return controller.exitCode();
}
