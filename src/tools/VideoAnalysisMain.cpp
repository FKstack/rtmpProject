#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "media/LatencyMarkerCodec.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace {

struct Roi { int x = 0; int y = 0; int width = 0; int height = 0; };

std::optional<Roi> parseRoi(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 4) return std::nullopt;
    bool ok[4] {};
    Roi roi {parts[0].toInt(&ok[0]), parts[1].toInt(&ok[1]),
             parts[2].toInt(&ok[2]), parts[3].toInt(&ok[3])};
    return std::all_of(std::begin(ok), std::end(ok), [](bool value) { return value; }) &&
                   roi.x >= 0 && roi.y >= 0 && roi.width >= 240 && roi.height >= 120
               ? std::optional<Roi>(roi) : std::nullopt;
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return -1.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::clamp(
        std::ceil(fraction * values.size()) - 1.0,
        0.0,
        static_cast<double>(values.size() - 1)
    ));
    return values[index];
}

struct SideStatistics
{
    std::uint64_t markerFrames = 0;
    std::uint64_t repeatedFrames = 0;
    std::uint64_t longIntervals = 0;
    std::optional<std::uint32_t> lastSequence;
    double lastMarkerPts = -1.0;
    std::map<int, int> uniqueFramesPerSecond;
    std::map<std::uint32_t, double> firstPtsBySequence;

    void record(std::uint32_t sequence, double pts, double expectedFrameSeconds)
    {
        ++markerFrames;
        if (lastSequence.has_value() && *lastSequence == sequence) {
            ++repeatedFrames;
        } else {
            ++uniqueFramesPerSecond[static_cast<int>(std::floor(pts))];
            firstPtsBySequence.emplace(sequence, pts);
        }
        if (lastMarkerPts >= 0.0 && pts - lastMarkerPts > expectedFrameSeconds * 1.5) {
            ++longIntervals;
        }
        lastMarkerPts = pts;
        lastSequence = sequence;
    }

    QJsonObject toJson(std::uint64_t decodedFrames) const
    {
        QJsonArray perSecond;
        for (const auto &[second, count] : uniqueFramesPerSecond) {
            perSecond.append(QJsonObject {{QStringLiteral("second"), second},
                                          {QStringLiteral("contentFps"), count}});
        }
        return {
            {QStringLiteral("markerFrames"), static_cast<qint64>(markerFrames)},
            {QStringLiteral("markerRecognitionRate"), decodedFrames == 0 ? 0.0 : static_cast<double>(markerFrames) / decodedFrames},
            {QStringLiteral("repeatedFrames"), static_cast<qint64>(repeatedFrames)},
            {QStringLiteral("longFrameIntervals"), static_cast<qint64>(longIntervals)},
            {QStringLiteral("contentFpsPerSecond"), perSecond}
        };
    }
};

class MarkerRoiDecoder
{
public:
    explicit MarkerRoiDecoder(Roi roi) : roi_(roi), scaled_(1280 * 720) {}
    ~MarkerRoiDecoder()
    {
        if (toGray_ != nullptr) sws_freeContext(toGray_);
        if (scaleRoi_ != nullptr) sws_freeContext(scaleRoi_);
    }

    LatencyMarkerDecodeResult decode(const AVFrame *frame)
    {
        if (frame == nullptr || roi_.x + roi_.width > frame->width ||
            roi_.y + roi_.height > frame->height) return {};
        gray_.resize(static_cast<std::size_t>(frame->width) * frame->height);
        toGray_ = sws_getCachedContext(
            toGray_, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format),
            frame->width, frame->height, AV_PIX_FMT_GRAY8,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
        );
        if (toGray_ == nullptr) return {};
        std::uint8_t *grayData[] = {gray_.data(), nullptr, nullptr, nullptr};
        const int grayStride[] = {frame->width, 0, 0, 0};
        if (sws_scale(toGray_, frame->data, frame->linesize, 0, frame->height,
                      grayData, grayStride) <= 0) return {};
        scaleRoi_ = sws_getCachedContext(
            scaleRoi_, roi_.width, roi_.height, AV_PIX_FMT_GRAY8,
            1280, 720, AV_PIX_FMT_GRAY8,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if (scaleRoi_ == nullptr) return {};
        const std::uint8_t *roiData[] = {
            gray_.data() + static_cast<std::ptrdiff_t>(roi_.y) * frame->width + roi_.x,
            nullptr, nullptr, nullptr
        };
        const int roiStride[] = {frame->width, 0, 0, 0};
        std::uint8_t *scaledData[] = {scaled_.data(), nullptr, nullptr, nullptr};
        const int scaledStride[] = {1280, 0, 0, 0};
        if (sws_scale(scaleRoi_, roiData, roiStride, 0, roi_.height,
                      scaledData, scaledStride) <= 0) return {};
        return LatencyMarkerCodec::decode({scaled_.data(), 1280, 720, 1280}, 0);
    }

private:
    Roi roi_;
    SwsContext *toGray_ = nullptr;
    SwsContext *scaleRoi_ = nullptr;
    std::vector<std::uint8_t> gray_;
    std::vector<std::uint8_t> scaled_;
};

QString ffmpegError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption inputOption(QStringLiteral("input"), QStringLiteral("Comparison video."), QStringLiteral("path"));
    QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("Analysis JSON."), QStringLiteral("path"));
    QCommandLineOption leftOption(QStringLiteral("left-roi"), QStringLiteral("Left ROI x,y,w,h."), QStringLiteral("roi"));
    QCommandLineOption rightOption(QStringLiteral("right-roi"), QStringLiteral("Right ROI x,y,w,h."), QStringLiteral("roi"));
    parser.addOptions({inputOption, outputOption, leftOption, rightOption});
    parser.process(app);
    const QString inputPath = parser.value(inputOption);
    const QString outputPath = parser.value(outputOption);
    const auto leftRoi = parseRoi(parser.value(leftOption));
    const auto rightRoi = parseRoi(parser.value(rightOption));
    if (inputPath.isEmpty() || outputPath.isEmpty() || !leftRoi || !rightRoi) {
        qCritical() << "--input, --output, --left-roi and --right-roi are required.";
        return 1;
    }

    AVFormatContext *format = nullptr;
    int error = avformat_open_input(&format, inputPath.toUtf8().constData(), nullptr, nullptr);
    if (error < 0 || avformat_find_stream_info(format, nullptr) < 0) {
        qCritical() << "Cannot open input:" << ffmpegError(error);
        if (format != nullptr) avformat_close_input(&format);
        return 2;
    }
    const int streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) { avformat_close_input(&format); return 2; }
    AVStream *stream = format->streams[streamIndex];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (codec == nullptr || context == nullptr ||
        avcodec_parameters_to_context(context, stream->codecpar) < 0 ||
        avcodec_open2(context, codec, nullptr) < 0) {
        avcodec_free_context(&context); avformat_close_input(&format); return 2;
    }
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    MarkerRoiDecoder leftDecoder(*leftRoi);
    MarkerRoiDecoder rightDecoder(*rightRoi);
    SideStatistics leftStats;
    SideStatistics rightStats;
    std::uint64_t decodedFrames = 0;
    double firstPts = -1.0;
    double lastPts = -1.0;
    const double declaredFps = av_q2d(stream->avg_frame_rate);
    const double expectedFrameSeconds = declaredFps > 0.0 ? 1.0 / declaredFps : 1.0 / 60.0;

    auto receiveFrames = [&] {
        while (avcodec_receive_frame(context, frame) == 0) {
            ++decodedFrames;
            const std::int64_t timestamp = frame->best_effort_timestamp;
            const double pts = timestamp == AV_NOPTS_VALUE
                                   ? decodedFrames * expectedFrameSeconds
                                   : timestamp * av_q2d(stream->time_base);
            if (firstPts < 0.0) firstPts = pts;
            lastPts = pts;
            const auto left = leftDecoder.decode(frame);
            const auto right = rightDecoder.decode(frame);
            if (left.sourceSequence) leftStats.record(*left.sourceSequence, pts, expectedFrameSeconds);
            if (right.sourceSequence) rightStats.record(*right.sourceSequence, pts, expectedFrameSeconds);
            av_frame_unref(frame);
        }
    };
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == streamIndex && avcodec_send_packet(context, packet) >= 0) receiveFrames();
        av_packet_unref(packet);
    }
    avcodec_send_packet(context, nullptr);
    receiveFrames();

    std::vector<double> relativeDelayMs;
    for (const auto &[sequence, leftPts] : leftStats.firstPtsBySequence) {
        const auto right = rightStats.firstPtsBySequence.find(sequence);
        if (right != rightStats.firstPtsBySequence.end()) {
            relativeDelayMs.push_back((leftPts - right->second) * 1000.0);
        }
    }
    const double duration = firstPts >= 0.0 && lastPts >= firstPts
                                ? lastPts - firstPts + expectedFrameSeconds : 0.0;
    const double observedFps = duration > 0.0 ? decodedFrames / duration : 0.0;
    const double leftRecognition = decodedFrames == 0 ? 0.0 : static_cast<double>(leftStats.markerFrames) / decodedFrames;
    const double rightRecognition = decodedFrames == 0 ? 0.0 : static_cast<double>(rightStats.markerFrames) / decodedFrames;
    const bool gateEligible = observedFps >= 59.0 && duration >= 30.0 &&
                              leftRecognition >= 0.90 && rightRecognition >= 0.90;
    QJsonObject result {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("inputFile"), QFileInfo(inputPath).fileName()},
        {QStringLiteral("decodedFrames"), static_cast<qint64>(decodedFrames)},
        {QStringLiteral("durationSeconds"), duration},
        {QStringLiteral("declaredFps"), declaredFps},
        {QStringLiteral("observedFps"), observedFps},
        {QStringLiteral("gateEligible"), gateEligible},
        {QStringLiteral("left"), leftStats.toJson(decodedFrames)},
        {QStringLiteral("right"), rightStats.toJson(decodedFrames)},
        {QStringLiteral("matchedSourceFrames"), static_cast<qint64>(relativeDelayMs.size())},
        {QStringLiteral("relativeDelayP50Ms"), percentile(relativeDelayMs, 0.50)},
        {QStringLiteral("relativeDelayP95Ms"), percentile(relativeDelayMs, 0.95)},
        {QStringLiteral("relativeDelayMaxMs"), relativeDelayMs.empty() ? -1.0 : *std::max_element(relativeDelayMs.begin(), relativeDelayMs.end())},
        {QStringLiteral("qualificationNote"), gateEligible ? QStringLiteral("eligible") : QStringLiteral("Requires >=60 FPS, >=30 seconds, and >=90% marker recognition on both ROIs.")}
    };
    QFile output(outputPath);
    if (!QDir().mkpath(QFileInfo(outputPath).absolutePath()) ||
        !output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return 3;
    }
    output.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    avformat_close_input(&format);
    return 0;
}
