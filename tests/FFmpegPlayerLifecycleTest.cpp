#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include "media/FFmpegPlayer.h"

namespace {

bool containsState(
    const QSignalSpy &stateSpy,
    FFmpegPlayer::PlaybackState expectedState
)
{
    for (const QList<QVariant> &arguments : stateSpy) {
        if (!arguments.isEmpty() &&
            arguments.constFirst().value<FFmpegPlayer::PlaybackState>() == expectedState) {
            return true;
        }
    }
    return false;
}

} // namespace

class FFmpegPlayerLifecycleTest final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidUrls();
    void stopIsIdempotent();
    void reconnectWaitCanBeInterrupted();
    void decodesConfiguredLiveStream();
};

void FFmpegPlayerLifecycleTest::rejectsInvalidUrls()
{
    FFmpegPlayer player;
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    QVERIFY(!player.start(QString()));
    QVERIFY(!player.start(QStringLiteral("https://127.0.0.1/live/camera001")));
    QVERIFY(!player.start(QStringLiteral("rtmp:///live/camera001")));
    QCOMPARE(errorSpy.count(), 3);
    QVERIFY(!player.isRunning());
}

void FFmpegPlayerLifecycleTest::stopIsIdempotent()
{
    FFmpegPlayer player;
    player.stop();
    player.stop();
    QVERIFY(!player.isRunning());
    QCOMPARE(player.metricsSnapshot().decodedFrames, std::uint64_t {0});
}

void FFmpegPlayerLifecycleTest::reconnectWaitCanBeInterrupted()
{
    FFmpegPlayer player;
    QSignalSpy stateSpy(&player, &FFmpegPlayer::stateChanged);

    QVERIFY(player.start(QStringLiteral("rtmp://127.0.0.1:1/live/unavailable")));
    QVERIFY(!player.start(QStringLiteral("rtmp://127.0.0.1:1/live/second")));
    QTRY_VERIFY_WITH_TIMEOUT(
        containsState(stateSpy, FFmpegPlayer::PlaybackState::Reconnecting),
        6'000
    );

    QElapsedTimer stopTimer;
    stopTimer.start();
    player.stop();

    QVERIFY2(stopTimer.elapsed() < 4'000, "Stopping the reconnect loop took too long.");
    QVERIFY(!player.isRunning());

    player.stop();
    QVERIFY(!player.isRunning());
}

void FFmpegPlayerLifecycleTest::decodesConfiguredLiveStream()
{
    const QString streamUrl = qEnvironmentVariable("RTMP_MONITOR_TEST_URL");
    if (streamUrl.isEmpty()) {
        QSKIP("Set RTMP_MONITOR_TEST_URL to run the live RTMP integration check.");
    }

    FFmpegPlayer player;
    QSignalSpy frameSpy(&player, &FFmpegPlayer::frameReady);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    QVERIFY(player.start(streamUrl));
    QElapsedTimer frameTimer;
    frameTimer.start();
    while (frameSpy.isEmpty() && frameTimer.elapsed() < 15'000) {
        QTest::qWait(50);
    }
    if (frameSpy.isEmpty()) {
        const QString lastError = errorSpy.isEmpty()
            ? QStringLiteral("No FFmpeg error was reported.")
            : errorSpy.constLast().constFirst().toString();
        QVERIFY2(!frameSpy.isEmpty(), qPrintable(lastError));
    }

    const QImage image = qvariant_cast<QImage>(frameSpy.constFirst().constFirst());
    QVERIFY(!image.isNull());
    QCOMPARE(image.format(), QImage::Format_RGB888);
    QVERIFY(image.width() > 0);
    QVERIFY(image.height() > 0);

    player.stop();
    QVERIFY(!player.isRunning());
}

QTEST_GUILESS_MAIN(FFmpegPlayerLifecycleTest)

#include "FFmpegPlayerLifecycleTest.moc"
