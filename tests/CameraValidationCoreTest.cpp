#include <QtTest>

#include <QDateTime>
#include <QProcess>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "media/LatencyMarkerCodec.h"
#include "render/DisplayFrameRatePolicy.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

class CameraValidationCoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void crcIsStable();
    void markerRoundTripAndSequence();
    void timestampWrapIsHandled();
    void scaledMarkerDecodes();
    void corruptedMarkerFails();
    void oldTimestampOnlyMarkerRemainsCompatible();
    void h264CompressionPreservesMarker();
    void displayPolicy_data();
    void displayPolicy();
};

void CameraValidationCoreTest::crcIsStable()
{
    QCOMPARE(LatencyMarkerCodec::crc8(0U), std::uint8_t {0});
    QCOMPARE(LatencyMarkerCodec::crc8(0x12345678U), std::uint8_t {0x1c});
}

void CameraValidationCoreTest::markerRoundTripAndSequence()
{
    std::vector<std::uint8_t> luma(1280 * 720, 90);
    const qint64 now = 9'000'123'456LL;
    QVERIFY(LatencyMarkerCodec::encode({luma.data(), 1280, 720, 1280}, now - 137, 42));
    const auto decoded = LatencyMarkerCodec::decode({luma.data(), 1280, 720, 1280}, now);
    QCOMPARE(decoded.sourceTimestampMs.value_or(-1), now - 137);
    QCOMPARE(decoded.sourceSequence.value_or(0), std::uint32_t {42});
}

void CameraValidationCoreTest::timestampWrapIsHandled()
{
    std::vector<std::uint8_t> luma(1280 * 720, 90);
    const qint64 now = (qint64 {1} << 32) + 25;
    QVERIFY(LatencyMarkerCodec::encode({luma.data(), 1280, 720, 1280}, now - 40, 7));
    QCOMPARE(LatencyMarkerCodec::decode({luma.data(), 1280, 720, 1280}, now)
                 .sourceTimestampMs.value_or(-1), now - 40);
}

void CameraValidationCoreTest::scaledMarkerDecodes()
{
    std::vector<std::uint8_t> source(1280 * 720, 90);
    std::vector<std::uint8_t> scaled(640 * 360, 0);
    QVERIFY(LatencyMarkerCodec::encode({source.data(), 1280, 720, 1280}, 1000, 99));
    for (int y = 0; y < 360; ++y) {
        for (int x = 0; x < 640; ++x) {
            scaled[y * 640 + x] = source[(y * 2) * 1280 + x * 2];
        }
    }
    const auto decoded = LatencyMarkerCodec::decode({scaled.data(), 640, 360, 640}, 1100);
    QCOMPARE(decoded.sourceTimestampMs.value_or(-1), qint64 {1000});
    QCOMPARE(decoded.sourceSequence.value_or(0), std::uint32_t {99});
}

void CameraValidationCoreTest::corruptedMarkerFails()
{
    std::vector<std::uint8_t> luma(1280 * 720, 90);
    QVERIFY(LatencyMarkerCodec::encode({luma.data(), 1280, 720, 1280}, 1000, 10));
    std::fill(luma.begin() + 15 * 1280, luma.begin() + 90 * 1280, 90);
    const auto decoded = LatencyMarkerCodec::decode({luma.data(), 1280, 720, 1280}, 1100);
    QVERIFY(!decoded.sourceTimestampMs.has_value());
    QVERIFY(!decoded.sourceSequence.has_value());
}

void CameraValidationCoreTest::oldTimestampOnlyMarkerRemainsCompatible()
{
    std::vector<std::uint8_t> luma(1280 * 720, 16);
    QVERIFY(LatencyMarkerCodec::encode({luma.data(), 1280, 720, 1280}, 1000, 1));
    std::fill(luma.begin() + 60 * 1280, luma.begin() + 85 * 1280, 16);
    const auto decoded = LatencyMarkerCodec::decode({luma.data(), 1280, 720, 1280}, 1100);
    QCOMPARE(decoded.sourceTimestampMs.value_or(-1), qint64 {1000});
    QVERIFY(!decoded.sourceSequence.has_value());
}

void CameraValidationCoreTest::h264CompressionPreservesMarker()
{
    const AVCodec *encoder = avcodec_find_encoder_by_name("libx264");
    if (encoder == nullptr) {
        const QString ffmpeg = qEnvironmentVariable("RTMP_MONITOR_TEST_FFMPEG");
        if (ffmpeg.isEmpty()) {
            QSKIP("Set RTMP_MONITOR_TEST_FFMPEG to exercise external libx264 compression.");
        }
        QByteArray raw(1280 * 720 * 3 / 2, static_cast<char>(128));
        std::fill(raw.begin(), raw.begin() + 1280 * 720, char {90});
        QVERIFY(LatencyMarkerCodec::encode(
            {reinterpret_cast<std::uint8_t *>(raw.data()), 1280, 720, 1280},
            5000, 1234
        ));
        QProcess encode;
        encode.start(ffmpeg, {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-video_size"), QStringLiteral("1280x720"), QStringLiteral("-framerate"), QStringLiteral("30"),
            QStringLiteral("-i"), QStringLiteral("pipe:0"), QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
            QStringLiteral("-tune"), QStringLiteral("zerolatency"), QStringLiteral("-bf"), QStringLiteral("0"),
            QStringLiteral("-f"), QStringLiteral("h264"), QStringLiteral("pipe:1")
        });
        QVERIFY(encode.waitForStarted(5000));
        QCOMPARE(encode.write(raw), qint64 {raw.size()});
        encode.closeWriteChannel();
        QVERIFY2(encode.waitForFinished(20'000), qPrintable(encode.errorString()));
        QCOMPARE(encode.exitCode(), 0);
        const QByteArray compressed = encode.readAllStandardOutput();
        QVERIFY(!compressed.isEmpty());
        QProcess decode;
        decode.start(ffmpeg, {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("h264"), QStringLiteral("-i"), QStringLiteral("pipe:0"),
            QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")
        });
        QVERIFY(decode.waitForStarted(5000));
        QCOMPARE(decode.write(compressed), qint64 {compressed.size()});
        decode.closeWriteChannel();
        QVERIFY2(decode.waitForFinished(20'000), qPrintable(decode.errorString()));
        QCOMPARE(decode.exitCode(), 0);
        const QByteArray decodedBytes = decode.readAllStandardOutput();
        QVERIFY(decodedBytes.size() >= raw.size());
        const auto marker = LatencyMarkerCodec::decode(
            {reinterpret_cast<const std::uint8_t *>(decodedBytes.constData()), 1280, 720, 1280},
            5100
        );
        QCOMPARE(marker.sourceTimestampMs.value_or(-1), qint64 {5000});
        QCOMPARE(marker.sourceSequence.value_or(0), std::uint32_t {1234});
        return;
    }
    AVCodecContext *encodeContext = avcodec_alloc_context3(encoder);
    QVERIFY(encodeContext != nullptr);
    encodeContext->width = 1280;
    encodeContext->height = 720;
    encodeContext->pix_fmt = AV_PIX_FMT_YUV420P;
    encodeContext->time_base = {1, 30};
    encodeContext->framerate = {30, 1};
    encodeContext->gop_size = 30;
    encodeContext->max_b_frames = 0;
    encodeContext->bit_rate = 4'000'000;
    av_opt_set(encodeContext->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encodeContext->priv_data, "tune", "zerolatency", 0);
    QVERIFY(avcodec_open2(encodeContext, encoder, nullptr) >= 0);

    AVFrame *source = av_frame_alloc();
    QVERIFY(source != nullptr);
    source->format = AV_PIX_FMT_YUV420P;
    source->width = 1280;
    source->height = 720;
    source->pts = 0;
    QVERIFY(av_frame_get_buffer(source, 32) >= 0);
    QVERIFY(av_frame_make_writable(source) >= 0);
    for (int y = 0; y < 720; ++y) std::fill(source->data[0] + y * source->linesize[0], source->data[0] + y * source->linesize[0] + 1280, 90);
    for (int plane = 1; plane < 3; ++plane) {
        for (int y = 0; y < 360; ++y) std::fill(source->data[plane] + y * source->linesize[plane], source->data[plane] + y * source->linesize[plane] + 640, 128);
    }
    QVERIFY(LatencyMarkerCodec::encode({source->data[0], 1280, 720, source->linesize[0]}, 5000, 1234));
    AVPacket *packet = av_packet_alloc();
    QVERIFY(packet != nullptr);
    QVERIFY(avcodec_send_frame(encodeContext, source) >= 0);
    QVERIFY(avcodec_receive_packet(encodeContext, packet) >= 0);

    const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecContext *decodeContext = avcodec_alloc_context3(decoder);
    QVERIFY(decoder != nullptr && decodeContext != nullptr);
    QVERIFY(avcodec_open2(decodeContext, decoder, nullptr) >= 0);
    AVFrame *decodedFrame = av_frame_alloc();
    QVERIFY(decodedFrame != nullptr);
    QVERIFY(avcodec_send_packet(decodeContext, packet) >= 0);
    QVERIFY(avcodec_receive_frame(decodeContext, decodedFrame) >= 0);
    const auto decoded = LatencyMarkerCodec::decode(
        {decodedFrame->data[0], decodedFrame->width, decodedFrame->height, decodedFrame->linesize[0]},
        5100
    );
    QCOMPARE(decoded.sourceTimestampMs.value_or(-1), qint64 {5000});
    QCOMPARE(decoded.sourceSequence.value_or(0), std::uint32_t {1234});
    av_frame_free(&decodedFrame);
    avcodec_free_context(&decodeContext);
    av_packet_free(&packet);
    av_frame_free(&source);
    avcodec_free_context(&encodeContext);
}

void CameraValidationCoreTest::displayPolicy_data()
{
    QTest::addColumn<int>("request");
    QTest::addColumn<int>("platform");
    QTest::addColumn<int>("streams");
    QTest::addColumn<int>("expected");
    QTest::newRow("windows-auto-1") << 0 << 0 << 1 << 30;
    QTest::newRow("windows-auto-16") << 0 << 0 << 16 << 30;
    QTest::newRow("arm-auto-8") << 0 << 1 << 8 << 15;
    QTest::newRow("explicit-15") << 1 << 0 << 8 << 15;
    QTest::newRow("explicit-30-arm") << 2 << 1 << 16 << 30;
    QTest::newRow("explicit-60") << 3 << 0 << 1 << 60;
}

void CameraValidationCoreTest::displayPolicy()
{
    QFETCH(int, request);
    QFETCH(int, platform);
    QFETCH(int, streams);
    QFETCH(int, expected);
    const auto decision = DisplayFrameRatePolicy::decide(
        static_cast<DisplayFrameRateRequest>(request),
        static_cast<DisplayFrameRatePlatform>(platform),
        streams
    );
    QCOMPARE(decision.effectiveFps, expected);
    QVERIFY(decision.fallbackReason.isEmpty());
}

QTEST_APPLESS_MAIN(CameraValidationCoreTest)
#include "CameraValidationCoreTest.moc"
