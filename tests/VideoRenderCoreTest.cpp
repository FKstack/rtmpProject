#include <QtTest>
#include <QDateTime>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "media/LatestFrameMailbox.h"
#include "media/VideoFrame.h"
#include "render/RenderTypes.h"
#include "render/VideoRenderController.h"
#include "FfmpegVideoFrameAdapter.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {

VideoFrame makeFrame(
    std::uint64_t sequence,
    std::uint64_t session = 1,
    int width = 5,
    int height = 3,
    bool negativeStride = false,
    qint64 receivedMonotonicMs = 456,
    qint64 sourceTimestampMs = -1,
    qint64 pts = 123
)
{
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    std::vector<std::uint8_t> y(static_cast<std::size_t>(width * height));
    std::vector<std::uint8_t> u(
        static_cast<std::size_t>(chromaWidth * chromaHeight), 90
    );
    std::vector<std::uint8_t> v(
        static_cast<std::size_t>(chromaWidth * chromaHeight), 180
    );
    for (std::size_t index = 0; index < y.size(); ++index) {
        y[index] = static_cast<std::uint8_t>(index + 1);
    }
    std::array<VideoPlaneView, VideoFrame::kMaximumPlanes> planes {};
    planes[0] = {
        negativeStride ? y.data() + width * (height - 1) : y.data(),
        negativeStride ? -width : width,
        width,
        height
    };
    planes[1] = {u.data(), chromaWidth, chromaWidth, chromaHeight};
    planes[2] = {v.data(), chromaWidth, chromaWidth, chromaHeight};
    const auto frame = VideoFrame::copyFromPlanes(
        width,
        height,
        VideoPixelFormat::Yuv420P8,
        planes,
        pts,
        2,
        {1, 30},
        {},
        sequence,
        session,
        receivedMonotonicMs,
        sourceTimestampMs
    );
    Q_ASSERT(frame.has_value());
    return *frame;
}

} // namespace

class VideoRenderCoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void frameOwnsOddSizedPlanesAndNegativeStride();
    void metadataFallbackAndHdrBoundary();
    void ffmpegFrameReferenceOutlivesOriginal();
    void mailboxIsCapacityOneAndRejectsStaleFrames();
    void mailboxRecordsFinalRenderLatency();
    void audioSyncClockFallsBackWhenPresentationIsStale();
    void mailboxHandlesConcurrentProducers();
    void dirtyAndPlacementAreDeterministic();
};

void VideoRenderCoreTest::frameOwnsOddSizedPlanesAndNegativeStride()
{
    const VideoFrame positive = makeFrame(1);
    QCOMPARE(positive.planeCount(), 3);
    QCOMPARE(positive.plane(0).rowBytes, 5);
    QCOMPARE(positive.plane(1).rowBytes, 3);
    QCOMPARE(positive.plane(1).rows, 2);
    QCOMPARE(positive.plane(0).data[0], std::uint8_t(1));

    const VideoFrame negative = makeFrame(2, 1, 5, 3, true);
    QVERIFY(negative.isValid());
    QCOMPARE(negative.plane(0).stride, std::ptrdiff_t(5));
    QCOMPARE(negative.plane(0).data[0], std::uint8_t(11));
    QCOMPARE(negative.plane(0).data[10], std::uint8_t(1));
}

void VideoRenderCoreTest::metadataFallbackAndHdrBoundary()
{
    const auto sd = resolvedVideoColorDescription({}, 720, 576);
    QCOMPARE(sd.matrix, VideoMatrixCoefficients::Bt601);
    QCOMPARE(sd.range, VideoColorRange::Limited);
    const auto hd = resolvedVideoColorDescription({}, 1920, 1080);
    QCOMPARE(hd.matrix, VideoMatrixCoefficients::Bt709);
    QVERIFY(isSupportedSdrTransfer(VideoTransferFunction::Bt2020_10));
    QVERIFY(!isSupportedSdrTransfer(VideoTransferFunction::Pq));
    QVERIFY(!isSupportedSdrTransfer(VideoTransferFunction::Hlg));
}

void VideoRenderCoreTest::ffmpegFrameReferenceOutlivesOriginal()
{
    AVFrame *source = av_frame_alloc();
    QVERIFY(source != nullptr);
    source->format = AV_PIX_FMT_YUV420P;
    source->width = 8;
    source->height = 6;
    source->pts = 77;
    source->colorspace = AVCOL_SPC_BT709;
    source->color_range = AVCOL_RANGE_MPEG;
    QVERIFY(av_frame_get_buffer(source, 32) >= 0);
    QVERIFY(av_frame_make_writable(source) >= 0);
    source->data[0][0] = 42;
    const auto adapted = FfmpegVideoFrameAdapter::adapt(
        source, {1, 25}, 9, 3, 100, 123456
    );
    QVERIFY(adapted.has_value());
    av_frame_free(&source);
    QVERIFY(adapted->isValid());
    QCOMPARE(adapted->pts(), qint64(77));
    QCOMPARE(adapted->sourceTimestampMs(), qint64(123456));
    QCOMPARE(adapted->plane(0).data[0], std::uint8_t(42));
}

void VideoRenderCoreTest::mailboxRecordsFinalRenderLatency()
{
    const qint64 receivedMonotonicMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        )
            .count();
    const qint64 sourceTimestampMs = QDateTime::currentMSecsSinceEpoch();
    LatestFrameMailbox mailbox;
    QVERIFY(mailbox.submit(makeFrame(
        1, 1, 5, 3, false, receivedMonotonicMs, sourceTimestampMs
    )));
    mailbox.recordRendered();
    mailbox.recordRendered();
    QVERIFY(mailbox.lastPresentedFrameAgeMs() >= 0);
    const auto stats = mailbox.stats();
    QCOMPARE(stats.rendered, std::uint64_t(2));
    QVERIFY(stats.internalLatencyP95Ms >= 0);
    QCOMPARE(stats.sourceLatencySamples, std::uint64_t(1));
    QVERIFY(stats.sourceLatencyP50Ms >= 0);
    QVERIFY(stats.sourceLatencyMaxMs <= 10'000);
    mailbox.clear();
    QCOMPARE(mailbox.lastPresentedFrameAgeMs(), qint64 {-1});
}

void VideoRenderCoreTest::audioSyncClockFallsBackWhenPresentationIsStale()
{
    LatestFrameMailbox mailbox;
    QVERIFY(mailbox.submit(makeFrame(
        1, 7, 5, 3, false, 456, -1, 123
    )));
    mailbox.recordRendered();
    QVERIFY(mailbox.submit(makeFrame(
        2, 7, 5, 3, false, 457, -1, 126
    )));

    QCOMPARE(mailbox.audioSyncMediaTimestampMs(7, 1'000), qint64(4'100));
    QCOMPARE(mailbox.audioSyncMediaTimestampMs(7, -1), qint64(4'200));
    QCOMPARE(mailbox.audioSyncMediaTimestampMs(8, -1), qint64(-1));
}

void VideoRenderCoreTest::mailboxIsCapacityOneAndRejectsStaleFrames()
{
    LatestFrameMailbox mailbox;
    QVERIFY(mailbox.submit(makeFrame(1)));
    QVERIFY(mailbox.submit(makeFrame(2)));
    QVERIFY(!mailbox.submit(makeFrame(1)));
    const auto latest = mailbox.consumeLatestAfter(0);
    QVERIFY(latest.has_value());
    QCOMPARE(latest->sequence(), std::uint64_t(2));
    const auto stats = mailbox.stats();
    QCOMPARE(stats.submitted, std::uint64_t(2));
    QCOMPARE(stats.overwritten, std::uint64_t(1));
    QCOMPARE(stats.rejectedStale, std::uint64_t(1));
    mailbox.clear();
    QVERIFY(!mailbox.latestAfter(0).has_value());
}

void VideoRenderCoreTest::mailboxHandlesConcurrentProducers()
{
    LatestFrameMailbox mailbox;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([producer, &mailbox] {
            for (std::uint64_t sequence = producer + 1;
                 sequence <= 400;
                 sequence += 4) {
                (void)mailbox.submit(makeFrame(sequence));
            }
        });
    }
    for (auto &producer : producers) {
        producer.join();
    }
    QCOMPARE(mailbox.latestSequence(), std::uint64_t(400));
    QVERIFY(mailbox.stats().submitted >= 1);
}

void VideoRenderCoreTest::dirtyAndPlacementAreDeterministic()
{
    auto mailbox = std::make_shared<LatestFrameMailbox>();
    VideoRenderController controller;
    controller.registerStream(7, mailbox);
    (void)controller.consumeDirty();
    QVERIFY(mailbox->submit(makeFrame(1)));
    QVERIFY((controller.pendingDirty() &
             renderDirtyBit(RenderDirtyFlag::Frame)) != 0U);
    controller.markDirty(RenderDirtyFlag::Layout);
    controller.markDirty(RenderDirtyFlag::Layout);
    QVERIFY(controller.dirtyState()->mergeCount() >= 1);
    const auto consumed = controller.consumeFrame(7, 0);
    QVERIFY(consumed.has_value());

    const VideoPlacement contain = calculateVideoPlacement(
        QRectF(0, 0, 100, 100), QSize(200, 100), VideoDisplayMode::Contain
    );
    QCOMPARE(contain.targetRect, QRectF(0, 25, 100, 50));
    const VideoPlacement cover = calculateVideoPlacement(
        QRectF(0, 0, 100, 100), QSize(200, 100), VideoDisplayMode::Cover
    );
    QCOMPARE(cover.targetRect, QRectF(0, 0, 100, 100));
    QCOMPARE(cover.sourceUv, QRectF(0.25, 0, 0.5, 1));

    const QRectF ultraWideViewport(0, 0, 400, 100);
    const VideoPlacement ultraWideContain = calculateVideoPlacement(
        ultraWideViewport, QSize(160, 90), VideoDisplayMode::Contain
    );
    QCOMPARE(ultraWideContain.targetRect.height(), qreal(100));
    QVERIFY(qAbs(ultraWideContain.targetRect.width() - 1600.0 / 9.0) < 0.001);
    QVERIFY(ultraWideContain.targetRect.left() > 100.0);

    const VideoPlacement ultraWideCover = calculateVideoPlacement(
        ultraWideViewport, QSize(160, 90), VideoDisplayMode::Cover
    );
    QCOMPARE(ultraWideCover.targetRect, ultraWideViewport);
    QCOMPARE(ultraWideCover.sourceUv.x(), qreal(0));
    QCOMPARE(ultraWideCover.sourceUv.width(), qreal(1));
    QVERIFY(qAbs(ultraWideCover.sourceUv.y() - 5.0 / 18.0) < 0.001);
    QVERIFY(qAbs(ultraWideCover.sourceUv.height() - 4.0 / 9.0) < 0.001);

    const QRectF standardViewport(0, 0, 160, 90);
    const VideoPlacement fourByThree = calculateVideoPlacement(
        standardViewport, QSize(80, 60), VideoDisplayMode::Contain
    );
    QCOMPARE(fourByThree.targetRect, QRectF(20, 0, 120, 90));
    QCOMPARE(fourByThree.sourceUv, QRectF(0, 0, 1, 1));
    const VideoPlacement portrait = calculateVideoPlacement(
        standardViewport, QSize(90, 160), VideoDisplayMode::Contain
    );
    QVERIFY(qAbs(portrait.targetRect.width() - 50.625) < 0.001);
    QVERIFY(qAbs(portrait.targetRect.center().x() -
                 standardViewport.center().x()) < 0.001);
    QCOMPARE(portrait.targetRect.height(), qreal(90));
    QCOMPARE(portrait.sourceUv, QRectF(0, 0, 1, 1));
}

QTEST_GUILESS_MAIN(VideoRenderCoreTest)

#include "VideoRenderCoreTest.moc"
