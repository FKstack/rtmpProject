#include "publisher/CameraH264Policy.h"
#include "publisher/CameraH264PublisherSource.h"

#include <QtTest>

#include <condition_variable>
#include <atomic>
#include <mutex>
#include <thread>

using namespace rtmp_monitor::publisher;
using namespace rtmp_monitor::publisher::camera_detail;

class CameraH264PublisherSourceTest final : public QObject
{
    Q_OBJECT
private slots:
    void nativePreferenceAndFallbackAreBounded()
    {
        NativeH264Evidence native;
        native.profileIdc = 66;
        native.profileIop = 0xe0;
        native.levelIdc = 31;
        native.hasBFrames = false;
        native.hasSps = true;
        native.hasPps = true;
        native.maximumIdrGapFrames = 30;
        QCOMPARE(chooseCapturePath(native, true), CapturePath::NativeH264);
        native.levelIdc = 32;
        QCOMPARE(chooseCapturePath(native, true), CapturePath::MfEncodedNv12);
        QCOMPARE(chooseCapturePath(native, false), CapturePath::None);
    }

    void nativePreflightUsesActualNalEvidence()
    {
        NativeH264Preflight preflight;
        preflight.observe({
            0,0,0,1,0x67,66,0xe0,31,
            0,0,0,1,0x68,0xce,
            0,0,0,1,0x65,0x88
        }, 0);
        preflight.observe({0,0,0,1,0x65,0x99}, 30);
        QCOMPARE(
            chooseCapturePath(preflight.evidence(), false),
            CapturePath::NativeH264
        );
        NativeH264Preflight withBFrame;
        withBFrame.observe({
            0,0,0,1,0x67,66,0xe0,31,
            0,0,0,1,0x68,0xce,
            0,0,0,1,0x65,0x88,
            0,0,0,1,0x41,0xa0
        }, 0);
        withBFrame.observe({0,0,0,1,0x65,0x99}, 30);
        QVERIFY(withBFrame.evidence().hasBFrames);
        QCOMPARE(
            chooseCapturePath(withBFrame.evidence(), true),
            CapturePath::MfEncodedNv12
        );
    }

    void timestampsStartAtZeroAndRepairRegression()
    {
        TimestampNormalizer normalizer;
        QCOMPARE(normalizer.next(900'000), 0);
        QCOMPARE(normalizer.next(933'333), 33'333);
        QCOMPARE(normalizer.next(933'333), 66'666);
        QCOMPARE(normalizer.next(920'000), 99'999);
        QCOMPARE(normalizer.next(std::nullopt), 133'332);
    }

    void recoveryIdrCarriesParameterSets()
    {
        const std::vector<std::uint8_t> spsPpsIdr {
            0,0,0,1,0x67,0x42,0xe0,0x1f,
            0,0,0,1,0x68,0xce,0x06,
            0,0,0,1,0x65,0x88
        };
        AnnexBRecoveryPolicy policy;
        auto first = policy.process(spsPpsIdr, 0);
        QVERIFY(first.has_value());
        QVERIFY(first->keyFrame);
        policy.requireRecoveryIdr();
        const std::vector<std::uint8_t> delta {0,0,0,1,0x41,0x01};
        QVERIFY(!policy.process(delta, 33'333).has_value());
        const std::vector<std::uint8_t> idr {0,0,0,1,0x65,0x99};
        auto recovered = policy.process(idr, 66'666);
        QVERIFY(recovered.has_value());
        QVERIFY(recovered->annexB.size() > idr.size());
        QVERIFY(recovered->annexB.size() <= 4U * 1024U * 1024U);
    }

    void h264MfPreflightIsSyntheticAndStable()
    {
        const bool available = validateH264MfSynthetic();
#ifdef Q_OS_WIN
        if (!available) {
            QSKIP("blocked(h264_mf_preflight)");
        }
#endif
        NativeH264Evidence incompatible;
        QCOMPARE(
            chooseCapturePath(incompatible, available),
            available ? CapturePath::MfEncodedNv12 : CapturePath::None
        );
    }

    void repeatedStopIsSafeWithoutOpeningCamera()
    {
        CameraH264PublisherSource source;
        source.stop();
        source.stop();
        QCOMPARE(source.snapshot().running, false);
    }

    void privateSeamReportsOpenAndDeviceLossWithoutPhysicalCamera()
    {
        auto openFailure = CameraSourceTestAccess::create(
            [](std::uint32_t index, const PublisherSubmitCallback &,
               const std::function<bool()> &) {
                return index == 7U ? PublisherSourceError::OpenFailed
                                   : PublisherSourceError::CameraNotFound;
            },
            [] {}
        );
        QCOMPARE(openFailure->start(7, [](H264AccessUnit) {
            return H264SubmitResult::Accepted;
        }), PublisherSourceError::None);
        QCOMPARE(
            openFailure->waitForCompletion(std::chrono::seconds(1)),
            PublisherSourceError::OpenFailed
        );

        auto disappeared = CameraSourceTestAccess::create(
            [](std::uint32_t, const PublisherSubmitCallback &,
               const std::function<bool()> &) {
                return PublisherSourceError::DeviceLost;
            },
            [] {}
        );
        QCOMPARE(disappeared->start(0, [](H264AccessUnit) {
            return H264SubmitResult::Accepted;
        }), PublisherSourceError::None);
        QCOMPARE(
            disappeared->waitForCompletion(std::chrono::seconds(1)),
            PublisherSourceError::DeviceLost
        );
    }

    void privateSeamInterruptsBlockingReadWithinTwoSeconds()
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool interrupted = false;
        auto source = CameraSourceTestAccess::create(
            [&](std::uint32_t, const PublisherSubmitCallback &,
                const std::function<bool()> &stopping) {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return interrupted || stopping(); });
                return PublisherSourceError::Stopped;
            },
            [&] {
                const std::lock_guard lock(mutex);
                interrupted = true;
                changed.notify_all();
            }
        );
        QCOMPARE(source->start(0, [](H264AccessUnit) {
            return H264SubmitResult::Accepted;
        }), PublisherSourceError::None);
        QElapsedTimer elapsed;
        elapsed.start();
        source->stop();
        QVERIFY(elapsed.elapsed() < 2'000);
        source->stop();
        QCOMPARE(source->snapshot().error, PublisherSourceError::Stopped);
    }

    void waiterAndStopCanJoinTheSameWorkerConcurrently()
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool interrupted = false;
        auto source = CameraSourceTestAccess::create(
            [&](std::uint32_t, const PublisherSubmitCallback &,
                const std::function<bool()> &stopping) {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return interrupted || stopping(); });
                return PublisherSourceError::Stopped;
            },
            [&] {
                const std::lock_guard lock(mutex);
                interrupted = true;
                changed.notify_all();
            }
        );
        QCOMPARE(source->start(0, [](H264AccessUnit) {
            return H264SubmitResult::Accepted;
        }), PublisherSourceError::None);

        std::atomic<PublisherSourceError> waited {
            PublisherSourceError::InvalidState
        };
        std::thread waiter([&] {
            waited.store(
                source->waitForCompletion(std::chrono::seconds(2)),
                std::memory_order_release
            );
        });
        QTest::qWait(20);
        source->stop();
        waiter.join();
        QCOMPARE(
            waited.load(std::memory_order_acquire),
            PublisherSourceError::Stopped
        );
    }

    void failedWorkerCanBeStoppedAfterWaitWithoutChangingItsError()
    {
        auto source = CameraSourceTestAccess::create(
            [](std::uint32_t, const PublisherSubmitCallback &,
               const std::function<bool()> &) {
                return PublisherSourceError::CompatiblePathUnavailable;
            },
            [] {}
        );
        QCOMPARE(source->start(0, [](H264AccessUnit) {
            return H264SubmitResult::Accepted;
        }), PublisherSourceError::None);
        QCOMPARE(
            source->waitForCompletion(std::chrono::seconds(1)),
            PublisherSourceError::CompatiblePathUnavailable
        );
        source->stop();
        QCOMPARE(
            source->snapshot().error,
            PublisherSourceError::CompatiblePathUnavailable
        );
    }
};

QTEST_GUILESS_MAIN(CameraH264PublisherSourceTest)
#include "CameraH264PublisherSourceTest.moc"
