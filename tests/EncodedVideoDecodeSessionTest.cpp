#include <QByteArray>
#include <QSemaphore>
#include <QTest>

#include <atomic>
#include <vector>

#include "media/DecodeWorkerPool.h"
#include "media/EncodedVideoDecodeSession.h"
#include "media/LatestFrameMailbox.h"
#include "media/MultiStreamPlaybackManager.h"

namespace {

H264AccessUnit fixedBlackIdr(std::int64_t timestampUs = 0)
{
    const QByteArray bytes = QByteArray::fromBase64(
        "AAAAAWdCwArd7ARAAAADAEAAAAMAo8SJ4AAAAAFozg/IAAABZYiEOiYoAAkC4A=="
    );
    H264AccessUnit accessUnit;
    accessUnit.annexB.assign(bytes.cbegin(), bytes.cend());
    accessUnit.mediaTimestampUs = timestampUs;
    accessUnit.keyFrame = true;
    return accessUnit;
}

} // namespace

class EncodedVideoDecodeSessionTest final : public QObject
{
    Q_OBJECT

private slots:
    void boundedIngressReportsEveryRejection();
    void fixedAnnexBAccessUnitReachesTheSharedMailbox();
    void managerHandleOwnsOneGenerationAndRevokesSafely();
};

void EncodedVideoDecodeSessionTest::boundedIngressReportsEveryRejection()
{
    DecodeWorkerPool pool(1);
    QSemaphore workerStarted;
    QSemaphore releaseWorker;
    QVERIFY(pool.post(0, [&workerStarted, &releaseWorker] {
        workerStarted.release();
        releaseWorker.acquire();
    }));
    QVERIFY(workerStarted.tryAcquire(1, 2'000));

    PlaybackPerformanceOptions options;
    options.maximumQueuedPackets = 1;
    options.maximumQueuedBytes = 128;
    EncodedVideoDecodeSession session(
        7, QStringLiteral("External"), &pool, options
    );
    QVERIFY(session.beginExternalGeneration(41));

    SessionMediaSample wrongGeneration {
        40, fixedBlackIdr()
    };
    QCOMPARE(
        session.submit(std::move(wrongGeneration)),
        H264SubmitResult::InvalidGeneration
    );

    SessionMediaSample empty {41, {}};
    QCOMPARE(
        session.submit(std::move(empty)),
        H264SubmitResult::InvalidAccessUnit
    );

    H264AccessUnit oversized = fixedBlackIdr();
    oversized.annexB.resize(129, 0x55);
    QCOMPARE(
        session.submit({41, std::move(oversized)}),
        H264SubmitResult::DroppedCapacity
    );

    H264AccessUnit nonKey = fixedBlackIdr();
    nonKey.keyFrame = false;
    QCOMPARE(
        session.submit({41, std::move(nonKey)}),
        H264SubmitResult::DroppedUntilKeyframe
    );
    QCOMPARE(
        session.submit({41, fixedBlackIdr()}),
        H264SubmitResult::Accepted
    );

    H264AccessUnit overflow = fixedBlackIdr(33'333);
    overflow.keyFrame = false;
    QCOMPARE(
        session.submit({41, std::move(overflow)}),
        H264SubmitResult::DroppedCapacity
    );

    releaseWorker.release();
    session.closeGeneration(41);
    session.closeGeneration(41);
    QCOMPARE(
        session.submit({41, fixedBlackIdr()}),
        H264SubmitResult::Closed
    );
    QCOMPARE(session.metricsSnapshot(DeviceStatus::Disconnected).queuePackets, 0);
    pool.stop();
}

void EncodedVideoDecodeSessionTest::
fixedAnnexBAccessUnitReachesTheSharedMailbox()
{
    DecodeWorkerPool pool(1);
    PlaybackPerformanceOptions options;
    options.maximumQueuedPackets = 4;
    options.maximumQueuedBytes = 4 * 1024;
    std::atomic_bool playing {false};
    EncodedVideoDecodeSession session(
        8,
        QStringLiteral("Fixed sample"),
        &pool,
        options,
        [&playing](DeviceStatus state, std::uint64_t generation) {
            if (state == DeviceStatus::Playing && generation == 52) {
                playing.store(true, std::memory_order_release);
            }
        }
    );
    QVERIFY(session.beginExternalGeneration(52));
    QCOMPARE(
        session.submit({52, fixedBlackIdr()}),
        H264SubmitResult::Accepted
    );

    const auto mailbox = session.frameMailbox();
    QTRY_VERIFY_WITH_TIMEOUT(
        mailbox->latestAfter(0).has_value(),
        3'000
    );
    const auto frame = mailbox->latestAfter(0);
    QVERIFY(frame.has_value());
    QCOMPARE(frame->width(), 16);
    QCOMPARE(frame->height(), 16);
    QCOMPARE(frame->sessionGeneration(), std::uint64_t {52});
    QVERIFY(playing.load(std::memory_order_acquire));

    session.close();
    QVERIFY(!mailbox->latestAfter(0).has_value());
    pool.stop();
}

void EncodedVideoDecodeSessionTest::
managerHandleOwnsOneGenerationAndRevokesSafely()
{
    MultiStreamPlaybackManager manager;
    EncodedVideoInputHandle handle =
        manager.createEncodedVideoInput(QStringLiteral("P2P viewer"));
    QVERIFY(handle.isOpen());
    QVERIFY(handle.streamId() != kInvalidStreamId);
    QVERIFY(handle.generation() != 0);
    QCOMPARE(manager.streamCount(), 1);
    QVERIFY(manager.streamIds().contains(handle.streamId()));
    QVERIFY(manager.frameMailbox(handle.streamId()) != nullptr);
    QVERIFY(!manager.selectAudioStream(handle.streamId()));

    SessionMediaSample stale {
        handle.generation() + 1,
        fixedBlackIdr()
    };
    QCOMPARE(
        handle.submit(std::move(stale)),
        H264SubmitResult::InvalidGeneration
    );

    const StreamId streamId = handle.streamId();
    handle.close();
    handle.close();
    QVERIFY(!handle.isOpen());
    QVERIFY(!manager.isStreamRunning(streamId));
    QCOMPARE(
        handle.submit(fixedBlackIdr()),
        H264SubmitResult::Closed
    );
    QTRY_COMPARE_WITH_TIMEOUT(
        manager.streamMetrics(streamId).state,
        QStringLiteral("disconnected"),
        1'000
    );
    QVERIFY(manager.removeStream(streamId));
    QCOMPARE(manager.streamCount(), 0);

    for (int cycle = 0; cycle < 9; ++cycle) {
        EncodedVideoInputHandle repeated =
            manager.createEncodedVideoInput(
                QStringLiteral("Repeated %1").arg(cycle)
            );
        QVERIFY(repeated.isOpen());
        const StreamId repeatedId = repeated.streamId();
        repeated.close();
        repeated.close();
        QVERIFY(manager.removeStream(repeatedId));
    }
    QCOMPARE(manager.streamCount(), 0);
}

QTEST_GUILESS_MAIN(EncodedVideoDecodeSessionTest)

#include "EncodedVideoDecodeSessionTest.moc"
