#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

#include <atomic>

#include "media/DecodeWorkerPool.h"
#include "media/MultiStreamPlaybackManager.h"

namespace {

QStringList unavailableStreamUrls()
{
    QStringList urls;
    for (int camera = 1; camera <= 16; ++camera) {
        urls.append(
            QStringLiteral("rtmp://127.0.0.1:1/live/camera%1")
                .arg(camera, 3, 10, QLatin1Char('0'))
        );
    }
    return urls;
}

bool containsState(
    const QSignalSpy &spy,
    StreamId streamId,
    DeviceStatus state
)
{
    for (const QList<QVariant> &arguments : spy) {
        if (arguments.size() >= 2 &&
            arguments.at(0).toULongLong() == streamId &&
            arguments.at(1).value<DeviceStatus>() == state) {
            return true;
        }
    }
    return false;
}

QList<QVariant> reconnectForStream(
    const QSignalSpy &spy,
    StreamId streamId
)
{
    for (const QList<QVariant> &arguments : spy) {
        if (arguments.size() >= 3 &&
            arguments.at(0).toULongLong() == streamId) {
            return arguments;
        }
    }
    return {};
}

} // namespace

class MultiStreamPlaybackManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void decodeWorkerAssignmentIsStable();
    void dynamicConnectionsKeepStableIds();
    void oneInvalidUrlDoesNotBlockOtherStreams();
    void stopAllInterruptsSixteenStreams();
    void metricsFileIsAtomicAndOmitsUrls();
    void decodesConfiguredLiveStreams();
};

void MultiStreamPlaybackManagerTest::initTestCase()
{
    qRegisterMetaType<StreamMetrics>();
}

void MultiStreamPlaybackManagerTest::decodeWorkerAssignmentIsStable()
{
    DecodeWorkerPool pool(4);
    QCOMPARE(pool.workerCount(), 4);
    QCOMPARE(pool.workerIndexFor(1), pool.workerIndexFor(5));
    QVERIFY(pool.workerIndexFor(1) != pool.workerIndexFor(2));

    std::atomic_int completed {0};
    for (int task = 0; task < 16; ++task) {
        QVERIFY(pool.post(task % 4, [&completed] {
            completed.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    QTRY_COMPARE_WITH_TIMEOUT(
        completed.load(std::memory_order_relaxed), 16, 2'000
    );
    pool.stop();
    QVERIFY(!pool.post(0, [] {}));
}

void MultiStreamPlaybackManagerTest::dynamicConnectionsKeepStableIds()
{
    PlaybackPerformanceOptions options;
    options.decodeWorkerCount = 2;
    MultiStreamPlaybackManager manager(options);

    QList<StreamId> ids;
    for (int camera = 1; camera <= 16; ++camera) {
        const StreamId id = manager.addStream(
            QStringLiteral("Camera %1")
                .arg(camera, 2, 10, QLatin1Char('0')),
            QStringLiteral("rtmp://127.0.0.1:1/live/camera%1")
                .arg(camera, 3, 10, QLatin1Char('0'))
        );
        QVERIFY(id != kInvalidStreamId);
        ids.append(id);
    }
    QCOMPARE(manager.streamCount(), 16);
    QCOMPARE(manager.streamIds(), ids);
    QCOMPARE(manager.addStream(
                 QStringLiteral("Camera 17"),
                 QStringLiteral("rtmp://127.0.0.1:1/live/camera017")
             ),
             kInvalidStreamId);

    const StreamId removedId = ids.at(7);
    QVERIFY(manager.removeStream(removedId));
    QVERIFY(!manager.removeStream(removedId));
    QCOMPARE(manager.streamCount(), 15);
    QVERIFY(!manager.streamIds().contains(removedId));

    const StreamId replacement = manager.addStream(
        QStringLiteral("Replacement"),
        QStringLiteral("rtmp://127.0.0.1:1/live/replacement")
    );
    QVERIFY(replacement > ids.constLast());
    QCOMPARE(manager.streamCount(), 16);
    manager.stopAll();
    manager.stopAll();
}

void MultiStreamPlaybackManagerTest::oneInvalidUrlDoesNotBlockOtherStreams()
{
    QStringList urls = unavailableStreamUrls();
    urls[0] = QStringLiteral("https://127.0.0.1/live/invalid");
    MultiStreamPlaybackManager manager(urls);
    QSignalSpy errorSpy(
        &manager, &MultiStreamPlaybackManager::errorOccurred
    );
    QSignalSpy stateSpy(
        &manager, &MultiStreamPlaybackManager::stateChanged
    );
    QSignalSpy reconnectSpy(
        &manager, &MultiStreamPlaybackManager::reconnectScheduled
    );

    QCOMPARE(manager.startAll(), 15);
    const QList<StreamId> ids = manager.streamIds();
    QVERIFY(!manager.isStreamRunning(ids.at(0)));
    for (int index = 1; index < ids.size(); ++index) {
        QVERIFY(manager.isStreamRunning(ids.at(index)));
    }
    QVERIFY(!errorSpy.isEmpty());
    QCOMPARE(errorSpy.constFirst().at(0).toULongLong(), ids.at(0));
    QCOMPARE(
        errorSpy.constFirst().at(1).value<PlaybackError>().code,
        PlaybackErrorCode::InvalidConfiguration
    );

    QTRY_VERIFY_WITH_TIMEOUT(
        containsState(
            stateSpy,
            ids.at(1),
            DeviceStatus::Reconnecting
        ),
        6'000
    );
    QTRY_VERIFY_WITH_TIMEOUT(
        !reconnectForStream(reconnectSpy, ids.at(1)).isEmpty(),
        6'000
    );
    const QList<QVariant> reconnect =
        reconnectForStream(reconnectSpy, ids.at(1));
    QCOMPARE(reconnect.at(1).toInt(), 1);
    QCOMPARE(reconnect.at(2).toInt(), 3'000);

    manager.stopStream(ids.at(2));
    QVERIFY(!manager.isStreamRunning(ids.at(2)));
    QVERIFY(manager.isStreamRunning(ids.at(1)));
    QVERIFY(manager.isStreamRunning(ids.at(3)));
    manager.stopAll();
}

void MultiStreamPlaybackManagerTest::stopAllInterruptsSixteenStreams()
{
    MultiStreamPlaybackManager manager(unavailableStreamUrls());
    QCOMPARE(manager.startAll(), 16);

    QElapsedTimer timer;
    timer.start();
    manager.stopAll();
    QVERIFY2(timer.elapsed() < 5'000, "Stopping sixteen streams timed out.");
    for (StreamId id : manager.streamIds()) {
        QVERIFY(!manager.isStreamRunning(id));
    }
    manager.stopAll();
}

void MultiStreamPlaybackManagerTest::metricsFileIsAtomicAndOmitsUrls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("metrics.json"));

    MultiStreamPlaybackManager manager;
    const StreamId id = manager.addStream(
        QStringLiteral("Metrics Camera"),
        QStringLiteral("rtmp://127.0.0.1:1/live/private-path")
    );
    QVERIFY(id != kInvalidStreamId);
    manager.setMetricsOutputPath(path);
    QVERIFY(manager.startStream(id));

    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 3'000);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray payload = file.readAll();
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toInt(), 3);
    QCOMPARE(document.object().value(QStringLiteral("streamCount")).toInt(), 1);
    QCOMPARE(
        document.object().value(QStringLiteral("streams")).toArray().size(), 1
    );
    const QJsonObject stream = document.object()
                                   .value(QStringLiteral("streams"))
                                   .toArray()
                                   .first()
                                   .toObject();
    QVERIFY(stream.contains(QStringLiteral("submittedFrames")));
    QVERIFY(stream.contains(QStringLiteral("mailboxOverwrittenFrames")));
    QVERIFY(stream.contains(QStringLiteral("uploadedFrames")));
    QVERIFY(stream.contains(QStringLiteral("renderedFrames")));
    QVERIFY(stream.contains(QStringLiteral("textureBytes")));
    QVERIFY(document.object().value(QStringLiteral("renderer")).isObject());
    QVERIFY(document.object().value(QStringLiteral("renderStatistics")).isObject());
    QVERIFY(!payload.contains("private-path"));
    QVERIFY(!payload.contains("rtmp://"));
    manager.stopAll();
}

void MultiStreamPlaybackManagerTest::decodesConfiguredLiveStreams()
{
    const QString configured =
        qEnvironmentVariable("RTMP_MONITOR_TEST_URLS");
    if (configured.isEmpty()) {
        QSKIP(
            "Set RTMP_MONITOR_TEST_URLS to sixteen semicolon-separated URLs."
        );
    }
    const QStringList urls =
        configured.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(urls.size(), 16);

    MultiStreamPlaybackManager manager(urls);
    QCOMPARE(manager.startAll(), 16);

    QSet<StreamId> streamsWithFrames;
    QElapsedTimer timer;
    timer.start();
    while (streamsWithFrames.size() < 16 && timer.elapsed() < 30'000) {
        QTest::qWait(50);
        for (const StreamId id : manager.streamIds()) {
            const auto mailbox = manager.frameMailbox(id);
            if (mailbox != nullptr && mailbox->latestAfter(0).has_value()) {
                streamsWithFrames.insert(id);
            }
        }
    }
    QCOMPARE(streamsWithFrames.size(), 16);
    manager.stopAll();
}

QTEST_GUILESS_MAIN(MultiStreamPlaybackManagerTest)

#include "MultiStreamPlaybackManagerTest.moc"
