#include <QAudioDevice>
#include <QMediaDevices>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>

#include "FfmpegSessionTypes.h"
#include "media/AudioPlaybackEngine.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace {

class ProbeCounter final : public AudioPlaybackObserver
{
public:
    void onAudioPlaybackSample(
        const AudioPlaybackProbeSample &sample
    ) noexcept override
    {
        if (sample.streamId == 1 && sample.sessionGeneration == 1 &&
            sample.mediaPtsUs >= 0 && sample.sinkWriteMonotonicUs >=
                sample.queuedMonotonicUs) {
            ++validSamples;
        }
    }

    std::atomic_int validSamples {0};
};

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           ).count();
}

} // namespace

class AudioPlaybackEngineTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAndStopAreSafe();
    void decodesNativeAacIntoDefaultOutput_data();
    void decodesNativeAacIntoDefaultOutput();
};

void AudioPlaybackEngineTest::defaultsAndStopAreSafe()
{
    AudioPlaybackEngine engine;
    QCOMPARE(engine.selectedStream(), kInvalidStreamId);
    QVERIFY(engine.isMuted());
    QCOMPARE(engine.state(1), AudioPlaybackState::Unavailable);

    auto currentConfiguration = std::make_shared<FfmpegCodecConfiguration>();
    QVERIFY(currentConfiguration->parameters != nullptr);
    currentConfiguration->parameters->codec_id = AV_CODEC_ID_AAC;
    currentConfiguration->kind = FfmpegTrackKind::Audio;
    engine.submitAudioConfiguration(1, currentConfiguration, 2);
    QCOMPARE(engine.state(1), AudioPlaybackState::Muted);

    engine.invalidateAudioSession(1, 1);
    QCOMPARE(engine.state(1), AudioPlaybackState::Muted);
    auto staleUnsupportedConfiguration =
        std::make_shared<FfmpegCodecConfiguration>();
    QVERIFY(staleUnsupportedConfiguration->parameters != nullptr);
    staleUnsupportedConfiguration->parameters->codec_id = AV_CODEC_ID_MP3;
    staleUnsupportedConfiguration->kind = FfmpegTrackKind::Audio;
    engine.submitAudioConfiguration(1, staleUnsupportedConfiguration, 1);
    QCOMPARE(engine.state(1), AudioPlaybackState::Muted);

    engine.invalidateAudioSession(1, 2);
    QCOMPARE(engine.state(1), AudioPlaybackState::Unavailable);
    engine.clearSelection();
    engine.stop();
    engine.stop();
}

void AudioPlaybackEngineTest::decodesNativeAacIntoDefaultOutput_data()
{
    QTest::addColumn<int>("channelCount");
    QTest::newRow("mono-planar-to-mono-s16") << 1;
    QTest::newRow("stereo-planar-to-mono-s16") << 2;
}

void AudioPlaybackEngineTest::decodesNativeAacIntoDefaultOutput()
{
    QFETCH(int, channelCount);
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        QSKIP("No default audio output device is available.");
    }
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (encoder == nullptr) QSKIP("Native FFmpeg AAC encoder is unavailable.");

    AVCodecContext *encoderContext = avcodec_alloc_context3(encoder);
    QVERIFY(encoderContext != nullptr);
    encoderContext->sample_rate = 48'000;
    encoderContext->bit_rate = 64'000;
    encoderContext->time_base = {1, encoderContext->sample_rate};
    av_channel_layout_default(&encoderContext->ch_layout, channelCount);
    // FFmpeg's native AAC encoder accepts planar float; keep the test on the
    // stable codec contract instead of the deprecated AVCodec::sample_fmts.
    encoderContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
    QVERIFY(avcodec_open2(encoderContext, encoder, nullptr) >= 0);

    auto configuration = std::make_shared<FfmpegCodecConfiguration>();
    QVERIFY(configuration->parameters != nullptr);
    QVERIFY(avcodec_parameters_from_context(
                configuration->parameters, encoderContext
            ) >= 0);
    configuration->kind = FfmpegTrackKind::Audio;
    configuration->timeBase = encoderContext->time_base;

    AudioPlaybackEngine engine;
    auto probe = std::make_shared<ProbeCounter>();
    engine.setQualificationObserver(probe);
    engine.submitAudioConfiguration(1, configuration, 1);
    engine.selectStream(1);
    engine.setMuted(true);

    AVFrame *frame = av_frame_alloc();
    AVPacket *encoded = av_packet_alloc();
    QVERIFY(frame != nullptr && encoded != nullptr);
    frame->format = encoderContext->sample_fmt;
    frame->sample_rate = encoderContext->sample_rate;
    QVERIFY(av_channel_layout_copy(
                &frame->ch_layout, &encoderContext->ch_layout
            ) >= 0);
    frame->nb_samples = std::max(encoderContext->frame_size, 1024);
    QVERIFY(av_frame_get_buffer(frame, 0) >= 0);

    int submittedPackets = 0;
    for (int index = 0; index < 12; ++index) {
        QVERIFY(av_frame_make_writable(frame) >= 0);
        av_samples_set_silence(
            frame->extended_data,
            0,
            frame->nb_samples,
            frame->ch_layout.nb_channels,
            static_cast<AVSampleFormat>(frame->format)
        );
        frame->pts = static_cast<qint64>(index) * frame->nb_samples;
        QVERIFY(avcodec_send_frame(encoderContext, frame) >= 0);
        while (avcodec_receive_packet(encoderContext, encoded) >= 0) {
            AVPacket *owned = av_packet_alloc();
            QVERIFY(owned != nullptr);
            av_packet_move_ref(owned, encoded);
            engine.submitAudioPacket(
                1, owned, monotonicMilliseconds(), 1
            );
            ++submittedPackets;
        }
    }
    QVERIFY(submittedPackets > 0);
    QTRY_VERIFY_WITH_TIMEOUT(
        engine.metricsSnapshot().decodedPackets > 0,
        5'000
    );
    QTRY_VERIFY_WITH_TIMEOUT(probe->validSamples.load() > 0, 5'000);
    QCOMPARE(engine.selectedStream(), StreamId(1));
    QVERIFY(engine.isMuted());
    QVERIFY(engine.metricsSnapshot().actualSinkBufferMs > 0);
    engine.setQualificationObserver({});

    av_packet_free(&encoded);
    av_frame_free(&frame);
    avcodec_free_context(&encoderContext);
}

QTEST_GUILESS_MAIN(AudioPlaybackEngineTest)

#include "AudioPlaybackEngineTest.moc"
