#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "RtmpMonitorBuildConfig.h"
#include "app/StreamConnectionController.h"
#include "logging/LogManager.h"
#include "logging/UserMessageService.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/MainWindow.h"
#include "ui/VideoGridWidget.h"

namespace {

bool containsPlayingState(const QSignalSpy &spy)
{
    for (const QList<QVariant> &arguments : spy) {
        if (arguments.size() >= 2 &&
            arguments.at(1).value<DeviceStatus>() == DeviceStatus::Playing) {
            return true;
        }
    }
    return false;
}

} // namespace

class LiveUiShutdownTest final : public QObject
{
    Q_OBJECT

private slots:
    void rendersLiveStreamAndShutsDownCleanly_data();
    void rendersLiveStreamAndShutsDownCleanly();
};

void LiveUiShutdownTest::rendersLiveStreamAndShutsDownCleanly_data()
{
    QTest::addColumn<RendererPreference>("rendererPreference");
    QTest::newRow("cpu") << RendererPreference::Cpu;
#if RTMP_MONITOR_HAS_OPENGL
    QTest::newRow("opengl") << RendererPreference::OpenGL;
#endif
}

void LiveUiShutdownTest::rendersLiveStreamAndShutsDownCleanly()
{
    const QString streamUrl = qEnvironmentVariable("RTMP_MONITOR_TEST_URL");
    if (streamUrl.isEmpty()) {
        QSKIP("Set RTMP_MONITOR_TEST_URL to run the live UI shutdown check.");
    }

    QFETCH(RendererPreference, rendererPreference);

    LogManager logManager;
    UserMessageService userMessageService;
    PlaybackPerformanceOptions options;
    options.decodeWorkerCount = 1;
    options.reconnectDelayMs = 100;
    MultiStreamPlaybackManager playbackManager(options);
    MainWindow mainWindow(rendererPreference);
    mainWindow.setUserMessageService(&userMessageService);
    StreamConnectionController connectionController(
        &mainWindow,
        &playbackManager,
        &logManager,
        &userMessageService
    );

    auto *videoGrid = mainWindow.findChild<VideoGridWidget *>();
    QVERIFY(videoGrid != nullptr);
    QSignalSpy stateSpy(
        &playbackManager,
        &MultiStreamPlaybackManager::stateChanged
    );
    QSignalSpy presentedSpy(videoGrid, &VideoGridWidget::surfacePresented);

    const StreamId streamId = connectionController.addConnection(
        QStringLiteral("Live shutdown camera"),
        streamUrl,
        true
    );
    QVERIFY(streamId != kInvalidStreamId);

    mainWindow.resize(960, 540);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QTRY_VERIFY_WITH_TIMEOUT(containsPlayingState(stateSpy), 15'000);
    QTRY_VERIFY_WITH_TIMEOUT(presentedSpy.count() > 0, 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(
        mainWindow.rendererRuntimeMetrics().renderedFrames > 0,
        5'000
    );

    // Mirror the production order that originally exposed the CRT failure:
    // closing the last top-level window ends app.exec(), then main.cpp stops
    // playback while the hidden MainWindow and its canvases still exist.
    mainWindow.close();
    playbackManager.stopAll();
    QVERIFY(!playbackManager.isStreamRunning(streamId));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QTEST_MAIN(LiveUiShutdownTest)

#include "LiveUiShutdownTest.moc"
