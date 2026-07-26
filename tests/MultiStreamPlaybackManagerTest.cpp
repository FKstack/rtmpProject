#include <QElapsedTimer>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include "media/MultiStreamPlaybackManager.h"

namespace {

bool containsStateForStream(
    const QSignalSpy &stateSpy,
    int expectedStreamIndex,
    FFmpegPlayer::PlaybackState expectedState
)
{
    for (const QList<QVariant> &arguments : stateSpy) {
        if (arguments.size() >= 2 &&
            arguments.at(0).toInt() == expectedStreamIndex &&
            arguments.at(1).value<FFmpegPlayer::PlaybackState>() == expectedState) {
            return true;
        }
    }
    return false;
}

QStringList unavailableStreamUrls()
{
    return {
        QStringLiteral("rtmp://127.0.0.1:1/live/camera001"),
        QStringLiteral("rtmp://127.0.0.1:1/live/camera002"),
        QStringLiteral("rtmp://127.0.0.1:1/live/camera003"),
        QStringLiteral("rtmp://127.0.0.1:1/live/camera004"),
    };
}

} // namespace

/**
 * @brief 验证多路播放器的索引路由、失败隔离和批量退出行为。
 *
 * 测试不创建 QWidget；管理器和全部 FFmpegPlayer 位于测试主线程，每个有效
 * RTMP URL 启动一条独立解码 QThread。
 */
class MultiStreamPlaybackManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void idleStopIsIdempotent();
    void oneInvalidUrlDoesNotBlockOtherStreams();
    void stopAllInterruptsAllStreams();
    void decodesConfiguredLiveStreams();
};

void MultiStreamPlaybackManagerTest::idleStopIsIdempotent()
{
    MultiStreamPlaybackManager manager(unavailableStreamUrls());

    QCOMPARE(manager.streamCount(), 4);
    QVERIFY(!manager.isStreamRunning(-1));
    QVERIFY(!manager.isStreamRunning(4));
    manager.stopStream(-1);
    manager.stopStream(4);
    manager.stopAll();
    manager.stopAll();

    for (int streamIndex = 0; streamIndex < manager.streamCount(); ++streamIndex) {
        QVERIFY(!manager.isStreamRunning(streamIndex));
    }
}

void MultiStreamPlaybackManagerTest::oneInvalidUrlDoesNotBlockOtherStreams()
{
    QStringList urls = unavailableStreamUrls();
    urls[0] = QStringLiteral("https://127.0.0.1/live/invalid");

    MultiStreamPlaybackManager manager(urls);
    QSignalSpy errorSpy(&manager, &MultiStreamPlaybackManager::errorOccurred);
    QSignalSpy stateSpy(&manager, &MultiStreamPlaybackManager::stateChanged);

    QCOMPARE(manager.startAll(), 3);
    QVERIFY(!manager.isStreamRunning(0));
    for (int streamIndex = 1; streamIndex < manager.streamCount(); ++streamIndex) {
        QVERIFY(manager.isStreamRunning(streamIndex));
    }

    QVERIFY(!errorSpy.isEmpty());
    QCOMPARE(errorSpy.constFirst().at(0).toInt(), 0);

    for (int streamIndex = 1; streamIndex < manager.streamCount(); ++streamIndex) {
        QTRY_VERIFY_WITH_TIMEOUT(
            containsStateForStream(
                stateSpy,
                streamIndex,
                FFmpegPlayer::PlaybackState::Reconnecting
            ),
            6'000
        );
    }

    manager.stopStream(2);
    QVERIFY(!manager.isStreamRunning(2));
    QVERIFY(manager.isStreamRunning(1));
    QVERIFY(manager.isStreamRunning(3));

    manager.stopAll();
    for (int streamIndex = 0; streamIndex < manager.streamCount(); ++streamIndex) {
        QVERIFY(!manager.isStreamRunning(streamIndex));
    }
}

void MultiStreamPlaybackManagerTest::stopAllInterruptsAllStreams()
{
    MultiStreamPlaybackManager manager(unavailableStreamUrls());
    QSignalSpy stateSpy(&manager, &MultiStreamPlaybackManager::stateChanged);

    QCOMPARE(manager.startAll(), 4);
    for (int streamIndex = 0; streamIndex < manager.streamCount(); ++streamIndex) {
        QTRY_VERIFY_WITH_TIMEOUT(
            containsStateForStream(
                stateSpy,
                streamIndex,
                FFmpegPlayer::PlaybackState::Reconnecting
            ),
            6'000
        );
    }

    QElapsedTimer stopTimer;
    stopTimer.start();
    manager.stopAll();

    QVERIFY2(
        stopTimer.elapsed() < 4'000,
        "Stopping all stream threads took too long."
    );
    for (int streamIndex = 0; streamIndex < manager.streamCount(); ++streamIndex) {
        QVERIFY(!manager.isStreamRunning(streamIndex));
    }

    manager.stopAll();
}

void MultiStreamPlaybackManagerTest::decodesConfiguredLiveStreams()
{
    const QString configuredUrls =
        qEnvironmentVariable("RTMP_MONITOR_TEST_URLS");
    if (configuredUrls.isEmpty()) {
        QSKIP(
            "Set RTMP_MONITOR_TEST_URLS to four semicolon-separated RTMP URLs "
            "to run the live multi-stream integration check."
        );
    }

    const QStringList streamUrls =
        configuredUrls.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(streamUrls.size(), 4);

    MultiStreamPlaybackManager manager(streamUrls);
    QSignalSpy frameSpy(&manager, &MultiStreamPlaybackManager::frameReady);
    QSignalSpy errorSpy(&manager, &MultiStreamPlaybackManager::errorOccurred);
    QCOMPARE(manager.startAll(), 4);

    QSet<int> streamsWithFrames;
    QElapsedTimer frameTimer;
    frameTimer.start();
    while (streamsWithFrames.size() < manager.streamCount() &&
           frameTimer.elapsed() < 20'000) {
        QTest::qWait(50);
        for (const QList<QVariant> &arguments : frameSpy) {
            if (arguments.size() < 2) {
                continue;
            }

            const int streamIndex = arguments.at(0).toInt();
            const QImage image = qvariant_cast<QImage>(arguments.at(1));
            QVERIFY(!image.isNull());
            QCOMPARE(image.format(), QImage::Format_RGB888);
            streamsWithFrames.insert(streamIndex);
        }
    }

    if (streamsWithFrames.size() != manager.streamCount()) {
        QString diagnostic = QStringLiteral(
            "Only %1/4 streams produced frames."
        ).arg(streamsWithFrames.size());
        if (!errorSpy.isEmpty()) {
            diagnostic += QStringLiteral(" Last error: %1")
                              .arg(errorSpy.constLast().at(1).toString());
        }
        QVERIFY2(
            streamsWithFrames.size() == manager.streamCount(),
            qPrintable(diagnostic)
        );
    }

    QCOMPARE(streamsWithFrames, QSet<int>({0, 1, 2, 3}));
    manager.stopAll();
}

QTEST_GUILESS_MAIN(MultiStreamPlaybackManagerTest)

#include "MultiStreamPlaybackManagerTest.moc"
