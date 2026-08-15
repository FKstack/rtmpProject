#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QQueue>
#include <QRegularExpression>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "media/LatencyMarkerCodec.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace {

class PreviewWidget final : public QWidget
{
public:
    PreviewWidget()
    {
        setWindowTitle(QStringLiteral("RtmpMonitor Camera Reference"));
        setObjectName(QStringLiteral("rtmpMonitorCameraReference"));
        resize(640, 360);
        setMinimumSize(320, 180);
    }

    void setFrame(QImage frame)
    {
        frame_ = std::move(frame);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (frame_.isNull()) return;
        const QSize fitted = frame_.size().scaled(size(), Qt::KeepAspectRatio);
        QRect target(QPoint(), fitted);
        target.moveCenter(rect().center());
        painter.drawImage(target, frame_);
    }

private:
    QImage frame_;
};

class CameraSource final : public QObject
{
public:
    CameraSource(
        QString ffmpeg,
        QString device,
        int width,
        int height,
        int fps,
        QStringList urls,
        QString metricsPath,
        bool synthetic,
        QString audioDevice,
        bool syntheticAudio,
        PreviewWidget *preview,
        QObject *parent = nullptr
    )
        : QObject(parent)
        , ffmpeg_(std::move(ffmpeg))
        , device_(std::move(device))
        , width_(width)
        , height_(height)
        , fps_(fps)
        , urls_(std::move(urls))
        , metricsPath_(std::move(metricsPath))
        , synthetic_(synthetic)
        , audioDevice_(std::move(audioDevice))
        , syntheticAudio_(syntheticAudio)
        , preview_(preview)
        , frameBytes_(width_ * height_ * 3 / 2)
    {
        connect(&capture_, &QProcess::readyReadStandardOutput,
                this, [this] { consumeCaptureOutput(); });
        connect(&capture_, &QProcess::readyReadStandardError,
                this, [this] {
                    captureLog_.append(capture_.readAllStandardError());
                    constexpr qsizetype maximumDiagnosticBytes = 16 * 1024;
                    if (captureLog_.size() > maximumDiagnosticBytes) {
                        captureLog_.remove(0, captureLog_.size() - maximumDiagnosticBytes);
                    }
                });
        connect(&encoder_, &QProcess::readyReadStandardError,
                this, [this] { consumeEncoderProgress(); });
        connect(&capture_, &QProcess::finished, this,
                [this](int code, QProcess::ExitStatus status) {
                    if (!stopping_) fail(QStringLiteral("capture exited"), code, status);
                });
        connect(&encoder_, &QProcess::finished, this,
                [this](int code, QProcess::ExitStatus status) {
                    if (!stopping_) fail(QStringLiteral("encoder exited"), code, status);
                });
        metricsTimer_.setInterval(1000);
        metricsTimer_.setTimerType(Qt::PreciseTimer);
        connect(&metricsTimer_, &QTimer::timeout, this, [this] { writeMetrics(); });
        submitTimer_.setTimerType(Qt::PreciseTimer);
        submitTimer_.setInterval(std::max(1, qRound(1000.0 / fps_)));
        connect(&submitTimer_, &QTimer::timeout,
                this, [this] { submitPendingFrame(); });
    }

    ~CameraSource() override
    {
        stop();
        if (sws_ != nullptr) sws_freeContext(sws_);
    }

    bool start()
    {
        if (urls_.isEmpty()) return false;
        const QFileInfo metricsInfo(metricsPath_);
        if (!metricsPath_.isEmpty() && !QDir().mkpath(metricsInfo.absolutePath())) {
            return false;
        }
        if (!metricsPath_.isEmpty()) {
            metricsFile_.setFileName(metricsPath_);
            if (!metricsFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                return false;
            }
        }

        QStringList encoderArgs {
            QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
            QStringLiteral("-loglevel"), QStringLiteral("warning"),
            QStringLiteral("-stats_period"), QStringLiteral("1"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-video_size"), QStringLiteral("%1x%2").arg(width_).arg(height_),
            QStringLiteral("-framerate"), QString::number(fps_),
            QStringLiteral("-i"), QStringLiteral("pipe:0")
        };
        const bool audioEnabled =
            syntheticAudio_ || !audioDevice_.trimmed().isEmpty();
        if (syntheticAudio_) {
            encoderArgs.append({
                QStringLiteral("-f"), QStringLiteral("lavfi"),
                QStringLiteral("-i"),
                QStringLiteral("sine=frequency=1000:sample_rate=48000")
            });
        } else if (!audioDevice_.trimmed().isEmpty()) {
            encoderArgs.append({
                QStringLiteral("-f"), QStringLiteral("dshow"),
                QStringLiteral("-rtbufsize"), QStringLiteral("16M"),
                QStringLiteral("-i"),
                QStringLiteral("audio=%1").arg(audioDevice_.trimmed())
            });
        }
        encoderArgs.append({
            // The tee muxer does not auto-select streams.
            QStringLiteral("-map"), QStringLiteral("0:v:0")
        });
        if (audioEnabled) {
            encoderArgs.append({
                QStringLiteral("-map"), QStringLiteral("1:a:0"),
                QStringLiteral("-c:a"), QStringLiteral("aac"),
                QStringLiteral("-profile:a"), QStringLiteral("aac_low"),
                QStringLiteral("-ar"), QStringLiteral("48000"),
                QStringLiteral("-ac"), QStringLiteral("1"),
                QStringLiteral("-b:a"), QStringLiteral("64k")
            });
        } else {
            encoderArgs.append(QStringLiteral("-an"));
        }
        encoderArgs.append({
            QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-preset"), QStringLiteral("ultrafast"),
            QStringLiteral("-tune"), QStringLiteral("zerolatency"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-g"), QString::number(fps_),
            QStringLiteral("-keyint_min"), QString::number(fps_),
            QStringLiteral("-bf"), QStringLiteral("0"),
            QStringLiteral("-sc_threshold"), QStringLiteral("0"),
            // tee cannot infer muxer requirements while opening the encoder;
            // FLV therefore needs global headers requested explicitly.
            QStringLiteral("-flags"), QStringLiteral("+global_header+low_delay"),
            QStringLiteral("-flush_packets"), QStringLiteral("1"),
            QStringLiteral("-progress"), QStringLiteral("pipe:2"),
            QStringLiteral("-f"), QStringLiteral("tee")
        });
        QStringList teeOutputs;
        for (const QString &url : urls_) {
            teeOutputs.append(QStringLiteral("[f=flv:onfail=abort]%1").arg(url));
        }
        encoderArgs.append(teeOutputs.join(QLatin1Char('|')));
        encoder_.setProcessChannelMode(QProcess::SeparateChannels);
        encoder_.start(ffmpeg_, encoderArgs, QIODevice::ReadWrite);
        if (!encoder_.waitForStarted(5000)) return false;

        QStringList captureArgs {
            QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
            QStringLiteral("-loglevel"), QStringLiteral("warning"),
            QStringLiteral("-fflags"), QStringLiteral("nobuffer")
        };
        if (synthetic_) {
            captureArgs.append({
                QStringLiteral("-f"), QStringLiteral("lavfi"),
                QStringLiteral("-i"),
                QStringLiteral("testsrc2=size=%1x%2:rate=%3").arg(width_).arg(height_).arg(fps_)
            });
        } else {
            captureArgs.append({
                QStringLiteral("-f"), QStringLiteral("dshow"),
                QStringLiteral("-rtbufsize"), QStringLiteral("256M"),
                // DirectShow selects compressed camera formats through the input
                // codec.  "-input_format" is an avfoundation option and is
                // rejected by current Windows FFmpeg builds.
                QStringLiteral("-vcodec"), QStringLiteral("mjpeg"),
                QStringLiteral("-video_size"), QStringLiteral("%1x%2").arg(width_).arg(height_),
                QStringLiteral("-framerate"), QString::number(fps_),
                QStringLiteral("-i"), QStringLiteral("video=%1").arg(device_)
            });
        }
        captureArgs.append({
            QStringLiteral("-an"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")
        });
        capture_.setProcessChannelMode(QProcess::SeparateChannels);
        capture_.start(ffmpeg_, captureArgs, QIODevice::ReadOnly);
        if (!capture_.waitForStarted(5000)) {
            stop();
            return false;
        }
        metricsTimer_.start();
        submitTimer_.start();
        return true;
    }

    void stop()
    {
        if (stopping_) return;
        stopping_ = true;
        metricsTimer_.stop();
        submitTimer_.stop();
        for (QProcess *process : {&capture_, &encoder_}) {
            if (process->state() != QProcess::NotRunning) {
                process->closeWriteChannel();
                process->terminate();
                if (!process->waitForFinished(3000)) {
                    process->kill();
                    process->waitForFinished(2000);
                }
            }
        }
        if (metricsFile_.isOpen()) metricsFile_.close();
    }

private:
    void consumeCaptureOutput()
    {
        captureBuffer_.append(capture_.readAllStandardOutput());
        const qsizetype completeFrames = captureBuffer_.size() / frameBytes_;
        if (completeFrames <= 0) return;
        capturedFrames_ += static_cast<std::uint64_t>(completeFrames);
        for (qsizetype index = 0; index < completeFrames; ++index) {
            QByteArray frame = captureBuffer_.left(frameBytes_);
            captureBuffer_.remove(0, frameBytes_);
            ++sourceSequence_;
            (void)LatencyMarkerCodec::encode(
                {reinterpret_cast<std::uint8_t *>(frame.data()), width_, height_, width_},
                QDateTime::currentMSecsSinceEpoch(),
                sourceSequence_
            );

            // Raw pipe delivery is commonly bursty (two 30 FPS frames every
            // ~66 ms). Keep a very small queue and pace both preview and encoder
            // at the requested source rate, otherwise the downstream capacity-
            // one mailbox can only present one frame from each burst.
            if (pendingFrames_.size() >= kMaximumPendingFrames) {
                pendingFrames_.dequeue();
                ++backpressureDrops_;
            }
            pendingFrames_.enqueue(std::move(frame));
        }
    }

    void submitPendingFrame()
    {
        if (pendingFrames_.isEmpty() || encoder_.state() != QProcess::Running ||
            encoder_.bytesToWrite() >= frameBytes_) return;
        const QByteArray frame = pendingFrames_.dequeue();
        const qint64 accepted = encoder_.write(frame);
        if (accepted == frame.size()) {
            ++submittedFrames_;
            updatePreview(frame);
        } else {
            ++backpressureDrops_;
        }
    }

    void updatePreview(const QByteArray &frame)
    {
        if (preview_ == nullptr) return;
        sws_ = sws_getCachedContext(
            sws_, width_, height_, AV_PIX_FMT_YUV420P,
            width_, height_, AV_PIX_FMT_BGRA,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
        );
        if (sws_ == nullptr) return;
        QImage image(width_, height_, QImage::Format_ARGB32);
        const std::uint8_t *source[] = {
            reinterpret_cast<const std::uint8_t *>(frame.constData()),
            reinterpret_cast<const std::uint8_t *>(frame.constData()) + width_ * height_,
            reinterpret_cast<const std::uint8_t *>(frame.constData()) + width_ * height_ * 5 / 4,
            nullptr
        };
        const int sourceStride[] = {width_, width_ / 2, width_ / 2, 0};
        std::uint8_t *destination[] = {image.bits(), nullptr, nullptr, nullptr};
        const int destinationStride[] = {
            static_cast<int>(image.bytesPerLine()), 0, 0, 0
        };
        if (sws_scale(sws_, source, sourceStride, 0, height_,
                      destination, destinationStride) > 0) {
            preview_->setFrame(std::move(image));
        }
    }

    void consumeEncoderProgress()
    {
        const QByteArray chunk = encoder_.readAllStandardError();
        encoderProgressBuffer_.append(chunk);
        encoderLog_.append(chunk);
        constexpr qsizetype maximumDiagnosticBytes = 16 * 1024;
        if (encoderLog_.size() > maximumDiagnosticBytes) {
            encoderLog_.remove(0, encoderLog_.size() - maximumDiagnosticBytes);
        }
        qsizetype newline = -1;
        while ((newline = encoderProgressBuffer_.indexOf('\n')) >= 0) {
            const QByteArray line = encoderProgressBuffer_.left(newline).trimmed();
            encoderProgressBuffer_.remove(0, newline + 1);
            if (line.startsWith("frame=")) {
                bool ok = false;
                const qulonglong value = line.mid(6).trimmed().toULongLong(&ok);
                if (ok) publishedFrames_ = value;
            }
        }
    }

    void writeMetrics()
    {
        QJsonObject object {
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("generatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("capturedFrames"), static_cast<qint64>(capturedFrames_)},
            {QStringLiteral("submittedFrames"), static_cast<qint64>(submittedFrames_)},
            {QStringLiteral("publishedFrames"), static_cast<qint64>(publishedFrames_)},
            {QStringLiteral("captureDroppedFrames"), static_cast<qint64>(captureDroppedFrames_)},
            {QStringLiteral("backpressureDrops"), static_cast<qint64>(backpressureDrops_)},
            {QStringLiteral("captureFps"), static_cast<double>(capturedFrames_ - lastCaptured_)},
            {QStringLiteral("submitFps"), static_cast<double>(submittedFrames_ - lastSubmitted_)},
            {QStringLiteral("publishFps"), static_cast<double>(publishedFrames_ - lastPublished_)},
            {QStringLiteral("encoderBytesPending"),
             encoder_.bytesToWrite() + pendingFrames_.size() * frameBytes_},
            {QStringLiteral("encoderQueueDepth"), pendingFrames_.size()},
            {QStringLiteral("captureRunning"), capture_.state() == QProcess::Running},
            {QStringLiteral("encoderRunning"), encoder_.state() == QProcess::Running},
            {QStringLiteral("streamCount"), urls_.size()},
            {QStringLiteral("lastError"), lastError_},
            {QStringLiteral("lastErrorDetail"), lastErrorDetail_}
        };
        lastCaptured_ = capturedFrames_;
        lastSubmitted_ = submittedFrames_;
        lastPublished_ = publishedFrames_;
        if (metricsFile_.isOpen()) {
            metricsFile_.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
            metricsFile_.write("\n");
            metricsFile_.flush();
        }
    }

    void fail(const QString &component, int code, QProcess::ExitStatus status)
    {
        lastError_ = component;
        lastErrorDetail_ = QStringLiteral("exitCode=%1 exitStatus=%2 captureLogTail=%3")
            .arg(code)
            .arg(static_cast<int>(status))
            .arg(QStringLiteral("capture=[%1] encoder=[%2]")
                .arg(QString::fromLocal8Bit(captureLog_).trimmed(),
                     QString::fromLocal8Bit(encoderLog_).trimmed()));
        qCritical().noquote() << component << "unexpectedly; code=" << code
                              << "status=" << status;
        writeMetrics();
        QCoreApplication::exit(2);
    }

    QString ffmpeg_;
    QString device_;
    int width_ = 1280;
    int height_ = 720;
    int fps_ = 30;
    QStringList urls_;
    QString metricsPath_;
    bool synthetic_ = false;
    QString audioDevice_;
    bool syntheticAudio_ = false;
    PreviewWidget *preview_ = nullptr;
    qsizetype frameBytes_ = 0;
    QProcess capture_;
    QProcess encoder_;
    QTimer metricsTimer_;
    QTimer submitTimer_;
    QFile metricsFile_;
    QByteArray captureBuffer_;
    QByteArray captureLog_;
    QByteArray encoderLog_;
    QByteArray encoderProgressBuffer_;
    // Still bounded and latest-frame-wins, but large enough to absorb the
    // observed sub-200 ms Windows scheduling hiccup without manufacturing a
    // source-sequence gap during an otherwise stable 30 FPS run.
    static constexpr qsizetype kMaximumPendingFrames = 8;
    QQueue<QByteArray> pendingFrames_;
    QString lastError_;
    QString lastErrorDetail_;
    SwsContext *sws_ = nullptr;
    bool stopping_ = false;
    std::uint32_t sourceSequence_ = 0;
    std::uint64_t capturedFrames_ = 0;
    std::uint64_t submittedFrames_ = 0;
    std::uint64_t publishedFrames_ = 0;
    std::uint64_t captureDroppedFrames_ = 0;
    std::uint64_t backpressureDrops_ = 0;
    std::uint64_t lastCaptured_ = 0;
    std::uint64_t lastSubmitted_ = 0;
    std::uint64_t lastPublished_ = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rtmp_monitor_camera_source"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption ffmpegOption(QStringLiteral("ffmpeg"), QStringLiteral("FFmpeg executable."), QStringLiteral("path"), QStringLiteral("ffmpeg"));
    QCommandLineOption deviceOption(QStringLiteral("device"), QStringLiteral("DirectShow video device."), QStringLiteral("name"), QStringLiteral("USB2.0 HD UVC WebCam"));
    QCommandLineOption widthOption(QStringLiteral("width"), QStringLiteral("Capture width."), QStringLiteral("pixels"), QStringLiteral("1280"));
    QCommandLineOption heightOption(QStringLiteral("height"), QStringLiteral("Capture height."), QStringLiteral("pixels"), QStringLiteral("720"));
    QCommandLineOption fpsOption(QStringLiteral("fps"), QStringLiteral("Capture FPS."), QStringLiteral("fps"), QStringLiteral("30"));
    QCommandLineOption urlOption(QStringLiteral("url"), QStringLiteral("RTMP output URL; repeat for tee outputs."), QStringLiteral("rtmp-url"));
    QCommandLineOption metricsOption(QStringLiteral("metrics-file"), QStringLiteral("Source JSONL output."), QStringLiteral("path"));
    QCommandLineOption syntheticOption(QStringLiteral("synthetic"), QStringLiteral("Use a testsrc2 input instead of DirectShow."));
    QCommandLineOption audioDeviceOption(
        QStringLiteral("audio-device"),
        QStringLiteral("Optional DirectShow audio device; omitted keeps video-only publishing."),
        QStringLiteral("name")
    );
    QCommandLineOption syntheticAudioOption(
        QStringLiteral("synthetic-audio"),
        QStringLiteral("Publish a 48 kHz mono synthetic qualification tone.")
    );
    parser.addOptions({ffmpegOption, deviceOption, widthOption, heightOption, fpsOption, urlOption, metricsOption, syntheticOption, audioDeviceOption, syntheticAudioOption});
    parser.process(app);
    const int width = parser.value(widthOption).toInt();
    const int height = parser.value(heightOption).toInt();
    const int fps = parser.value(fpsOption).toInt();
    const QStringList urls = parser.values(urlOption);
    if (width <= 0 || height <= 0 || (width % 2) != 0 || (height % 2) != 0 ||
        fps <= 0 || fps > 60 || urls.isEmpty() || urls.size() > 16 ||
        (parser.isSet(syntheticAudioOption) &&
         !parser.value(audioDeviceOption).trimmed().isEmpty()) ||
        std::any_of(urls.begin(), urls.end(), [](const QString &url) {
            return !url.startsWith(QStringLiteral("rtmp://"));
        })) {
        qCritical() << "Invalid dimensions, FPS, RTMP URL list, or audio options.";
        return 1;
    }
    PreviewWidget preview;
    preview.show();
    CameraSource source(
        parser.value(ffmpegOption), parser.value(deviceOption),
        width, height, fps, urls, parser.value(metricsOption),
        parser.isSet(syntheticOption), parser.value(audioDeviceOption),
        parser.isSet(syntheticAudioOption), &preview
    );
    if (!source.start()) {
        qCritical() << "Camera source failed to start.";
        return 2;
    }
    const int result = app.exec();
    source.stop();
    return result;
}
