#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include "media/FFmpegPlayer.h"

namespace {

bool containsState(
    const QSignalSpy &stateSpy,
    DeviceStatus expectedState
)
{
    for (const QList<QVariant> &arguments : stateSpy) {
        if (!arguments.isEmpty() &&
            arguments.constFirst().value<DeviceStatus>() == expectedState) {
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
    void invalidUrlSignalOrderRemainsCompatible();
    void stopIsIdempotent();
    void reconnectWaitCanBeInterrupted();
    void finiteReconnectLimitEndsInErrorAndResetsOnRestart();
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
    for (const QList<QVariant> &arguments : errorSpy) {
        QCOMPARE(
            arguments.constFirst().value<PlaybackError>().code,
            PlaybackErrorCode::InvalidConfiguration
        );
    }
    QVERIFY(!player.isRunning());
}

void FFmpegPlayerLifecycleTest::invalidUrlSignalOrderRemainsCompatible()
{
    QStringList events;
    // The observer storage must outlive player because the compatibility
    // façade emits Disconnected during explicit/destructor stop.
    FFmpegPlayer player;
    connect(
        &player,
        &FFmpegPlayer::errorOccurred,
        &player,
        [&events](const PlaybackError &) {
            events.append(QStringLiteral("error"));
        }
    );
    connect(
        &player,
        &FFmpegPlayer::stateChanged,
        &player,
        [&events](DeviceStatus) {
            events.append(QStringLiteral("state"));
        }
    );

    QVERIFY(!player.start(QStringLiteral("https://127.0.0.1/video")));
    QCOMPARE(
        events,
        QStringList({QStringLiteral("error"), QStringLiteral("state")})
    );
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
        containsState(stateSpy, DeviceStatus::Reconnecting),
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

void FFmpegPlayerLifecycleTest::
finiteReconnectLimitEndsInErrorAndResetsOnRestart()
{
    PlaybackPerformanceOptions options;
    options.decodeWorkerCount = 1;
    options.reconnectDelayMs = 50;
    options.maximumConsecutiveFailures = 2;
    FFmpegPlayer player(
        1,
        QStringLiteral("Limited Camera"),
        nullptr,
        options
    );
    QSignalSpy stateSpy(&player, &FFmpegPlayer::stateChanged);
    QSignalSpy reconnectSpy(&player, &FFmpegPlayer::reconnectScheduled);

    QVERIFY(player.start(
        QStringLiteral("rtmp://127.0.0.1:1/live/unavailable")
    ));
    QTRY_VERIFY_WITH_TIMEOUT(!player.isRunning(), 12'000);
    QVERIFY(containsState(stateSpy, DeviceStatus::Connecting));
    QVERIFY(containsState(stateSpy, DeviceStatus::Reconnecting));
    QVERIFY(containsState(stateSpy, DeviceStatus::Error));
    QVERIFY(!stateSpy.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(
        stateSpy.constLast()
            .constFirst()
            .value<DeviceStatus>(),
        DeviceStatus::Error,
        1'000
    );
    QCOMPARE(reconnectSpy.count(), 1);
    QCOMPARE(reconnectSpy.constFirst().at(0).toInt(), 1);
    QCOMPARE(reconnectSpy.constFirst().at(1).toInt(), 50);

    stateSpy.clear();
    reconnectSpy.clear();
    QVERIFY(player.start(
        QStringLiteral("rtmp://127.0.0.1:1/live/unavailable")
    ));
    QTRY_VERIFY_WITH_TIMEOUT(reconnectSpy.count() >= 1, 7'000);
    QCOMPARE(reconnectSpy.constFirst().at(0).toInt(), 1);
    player.stop();
    QVERIFY(containsState(stateSpy, DeviceStatus::Disconnected));
}

void FFmpegPlayerLifecycleTest::decodesConfiguredLiveStream()
{
    const QString streamUrl = qEnvironmentVariable("RTMP_MONITOR_TEST_URL");
    if (streamUrl.isEmpty()) {
        QSKIP("Set RTMP_MONITOR_TEST_URL to run the live RTMP integration check.");
    }

    FFmpegPlayer player;
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);
    const auto mailbox = player.frameMailbox();

    QVERIFY(player.start(streamUrl));
    QElapsedTimer frameTimer;
    frameTimer.start();
    while (!mailbox->latestAfter(0).has_value() &&
           frameTimer.elapsed() < 15'000) {
        QTest::qWait(50);
    }
    const auto frame = mailbox->latestAfter(0);
    if (!frame.has_value()) {
        const QString lastError = errorSpy.isEmpty()
            ? QStringLiteral("No FFmpeg error was reported.")
            : errorSpy.constLast()
                  .constFirst()
                  .value<PlaybackError>()
                  .technicalMessage;
        QVERIFY2(frame.has_value(), qPrintable(lastError));
    }

    QVERIFY(frame->isValid());
    QVERIFY(frame->pixelFormat() == VideoPixelFormat::Yuv420P8 ||
            frame->pixelFormat() == VideoPixelFormat::Nv12_8);
    QVERIFY(frame->width() > 0);
    QVERIFY(frame->height() > 0);

    player.stop();
    QVERIFY(!player.isRunning());
}

QTEST_GUILESS_MAIN(FFmpegPlayerLifecycleTest)

#include "FFmpegPlayerLifecycleTest.moc"
