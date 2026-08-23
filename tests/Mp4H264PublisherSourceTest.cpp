#include "publisher/Mp4H264PublisherSource.h"

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

using namespace rtmp_monitor::publisher;

namespace {

std::vector<std::uint8_t> nalTypes(const std::vector<std::uint8_t> &bytes)
{
    std::vector<std::uint8_t> result;
    for (std::size_t index = 0; index + 4 < bytes.size(); ++index) {
        std::size_t header = 0;
        if (bytes[index] == 0 && bytes[index + 1] == 0 &&
            bytes[index + 2] == 1) {
            header = index + 3;
        } else if (index + 4 < bytes.size() && bytes[index] == 0 &&
                   bytes[index + 1] == 0 && bytes[index + 2] == 0 &&
                   bytes[index + 3] == 1) {
            header = index + 4;
        }
        if (header > 0 && header < bytes.size()) {
            result.push_back(bytes[header] & 0x1fU);
            index = header;
        }
    }
    return result;
}

} // namespace

class Mp4H264PublisherSourceTest final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileIsRejectedSynchronously()
    {
        Mp4H264PublisherSource source;
        QCOMPARE(
            source.start("missing-week4-sample.mp4", [](H264AccessUnit) {
                return H264SubmitResult::Accepted;
            }),
            PublisherSourceError::FileNotFound
        );
    }

    void malformedFileFailsWithoutLeakingWorker()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("invalid.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("not-an-mp4"), qint64(10));
        file.close();

        Mp4H264PublisherSource source;
        QCOMPARE(
            source.start(path.toStdString(), [](H264AccessUnit) {
                return H264SubmitResult::Accepted;
            }),
            PublisherSourceError::None
        );
        QCOMPARE(
            source.waitForCompletion(std::chrono::seconds(5)),
            PublisherSourceError::OpenFailed
        );
        source.stop();
        QVERIFY(!source.snapshot().running);
    }

    void audioOnlyFileIsRejected()
    {
        verifyFixtureError(
            "RTMP_MONITOR_WEEK4_AUDIO_ONLY",
            PublisherSourceError::VideoStreamMissing
        );
    }

    void nonH264VideoIsRejected()
    {
        verifyFixtureError(
            "RTMP_MONITOR_WEEK4_NON_H264",
            PublisherSourceError::H264Required
        );
    }

    void h264WithBFramesIsRejected()
    {
        verifyFixtureError(
            "RTMP_MONITOR_WEEK4_B_FRAMES",
            PublisherSourceError::BFramesUnsupported
        );
    }

    void generatedFixtureProducesAnnexBAndPaces()
    {
        const QByteArray environment = qgetenv("RTMP_MONITOR_WEEK4_SAMPLE");
        if (environment.isEmpty()) {
            QSKIP("RTMP_MONITOR_WEEK4_SAMPLE is set by the Week 4 qualification script");
        }

        std::mutex mutex;
        std::vector<std::int64_t> timestamps;
        std::vector<std::uint8_t> firstKeyframe;
        const auto started = std::chrono::steady_clock::now();
        Mp4H264PublisherSource source;
        QCOMPARE(
            source.start(environment.toStdString(), [&](H264AccessUnit accessUnit) {
                const std::lock_guard lock(mutex);
                timestamps.push_back(accessUnit.mediaTimestampUs);
                if (accessUnit.keyFrame && firstKeyframe.empty()) {
                    firstKeyframe = accessUnit.annexB;
                }
                return H264SubmitResult::Accepted;
            }),
            PublisherSourceError::None
        );
        QCOMPARE(
            source.waitForCompletion(std::chrono::seconds(20)),
            PublisherSourceError::None
        );
        const auto elapsed = std::chrono::steady_clock::now() - started;
        QVERIFY(elapsed >= std::chrono::seconds(5));
        const PublisherSourceSnapshot snapshot = source.snapshot();
        QVERIFY(snapshot.completed);
        QVERIFY(snapshot.emittedAccessUnits >= 150);
        QVERIFY(snapshot.emittedKeyframes >= 5);

        const std::lock_guard lock(mutex);
        QVERIFY(std::is_sorted(timestamps.cbegin(), timestamps.cend()));
        const auto types = nalTypes(firstKeyframe);
        QVERIFY(std::find(types.cbegin(), types.cend(), 7) != types.cend());
        QVERIFY(std::find(types.cbegin(), types.cend(), 8) != types.cend());
        QVERIFY(std::find(types.cbegin(), types.cend(), 5) != types.cend());
    }

    void pacingWaitIsInterruptible()
    {
        const QByteArray environment = qgetenv("RTMP_MONITOR_WEEK4_SAMPLE");
        if (environment.isEmpty()) {
            QSKIP("RTMP_MONITOR_WEEK4_SAMPLE is set by the Week 4 qualification script");
        }
        Mp4H264PublisherSource source;
        QCOMPARE(
            source.start(environment.toStdString(), [](H264AccessUnit) {
                return H264SubmitResult::Accepted;
            }),
            PublisherSourceError::None
        );
        QTest::qSleep(100);
        const auto started = std::chrono::steady_clock::now();
        source.stop();
        QVERIFY(std::chrono::steady_clock::now() - started <
                std::chrono::seconds(2));
        QCOMPARE(source.snapshot().error, PublisherSourceError::Stopped);
    }

private:
    static void verifyFixtureError(
        const char *environmentName,
        PublisherSourceError expected
    )
    {
        const QByteArray path = qgetenv(environmentName);
        if (path.isEmpty()) {
            QSKIP("Week 4 qualification script supplies the negative fixture");
        }
        Mp4H264PublisherSource source;
        QCOMPARE(
            source.start(path.toStdString(), [](H264AccessUnit) {
                return H264SubmitResult::Accepted;
            }),
            PublisherSourceError::None
        );
        QCOMPARE(source.waitForCompletion(std::chrono::seconds(5)), expected);
        source.stop();
        QVERIFY(!source.snapshot().running);
    }
};

QTEST_GUILESS_MAIN(Mp4H264PublisherSourceTest)
#include "Mp4H264PublisherSourceTest.moc"
