#include <QFile>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "app/StreamConnectionController.h"
#include "logging/LogManager.h"
#include "logging/UserMessageService.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

namespace {

int countMessages(
    const QSignalSpy &spy,
    UserEventType type
)
{
    int count = 0;
    for (const QList<QVariant> &arguments : spy) {
        if (!arguments.isEmpty() &&
            arguments.constFirst().value<UserMessage>().type == type) {
            ++count;
        }
    }
    return count;
}

QString firstMessageText(
    const QSignalSpy &spy,
    UserEventType type
)
{
    for (const QList<QVariant> &arguments : spy) {
        if (arguments.isEmpty()) {
            continue;
        }
        const UserMessage message =
            arguments.constFirst().value<UserMessage>();
        if (message.type == type) {
            return message.text;
        }
    }
    return {};
}

} // namespace

class StreamConnectionControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void separatesUserSystemAndAuditOutputs();
    void profileConnectionGeneratesUrlAndValidates();
    void profileConnectionsRespectCapacity();
    void selectsStableControlTargetAndTracksPresence();
};

void StreamConnectionControllerTest::
separatesUserSystemAndAuditOutputs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LoggingOptions loggingOptions;
    loggingOptions.directoryPath = directory.path();
    loggingOptions.minimumLevel = LogLevel::Trace;
    loggingOptions.consoleEnabled = false;
    loggingOptions.repeatWindowMs = 60'000;

    LogManager logManager;
    QVERIFY(logManager.initialize(loggingOptions));
    UserMessageService userMessages(60'000);
    QSignalSpy messageSpy(
        &userMessages,
        &UserMessageService::messageAdded
    );

    PlaybackPerformanceOptions playbackOptions;
    playbackOptions.decodeWorkerCount = 1;
    playbackOptions.reconnectDelayMs = 50;
    playbackOptions.maximumConsecutiveFailures = 2;
    MultiStreamPlaybackManager playbackManager(playbackOptions);
    MainWindow window;
    window.setUserMessageService(&userMessages);
    StreamConnectionController controller(
        &window,
        &playbackManager,
        &logManager,
        &userMessages
    );

    QCOMPARE(
        controller.addConnection(
            QStringLiteral(""),
            QStringLiteral("invalid"),
            true,
            true
        ),
        kInvalidStreamId
    );
    QCOMPARE(countMessages(messageSpy, UserEventType::DeviceAddFailed), 1);

    const StreamId streamId = controller.addConnection(
        QStringLiteral("摄像头 01"),
        QStringLiteral("rtmp://127.0.0.1:1/live/private-key?token=x"),
        false,
        true
    );
    QVERIFY(streamId != kInvalidStreamId);
    QCOMPARE(countMessages(messageSpy, UserEventType::DeviceAdded), 1);
    emit playbackManager.stateChanged(
        streamId,
        DeviceStatus::Playing
    );
    QCOMPARE(
        countMessages(messageSpy, UserEventType::DeviceConnected),
        1
    );
    QCOMPARE(
        firstMessageText(
            messageSpy,
            UserEventType::DeviceConnected
        ),
        QStringLiteral("摄像头 01 连接成功")
    );
    QVERIFY(playbackManager.startStream(streamId));

    QCOMPARE(
        controller.addConnection(
            QStringLiteral("摄像头 01"),
            QStringLiteral("rtmp://127.0.0.1:1/live/duplicate"),
            false,
            true
        ),
        kInvalidStreamId
    );
    QCOMPARE(countMessages(messageSpy, UserEventType::DeviceAddFailed), 2);

    QTRY_VERIFY_WITH_TIMEOUT(
        countMessages(
            messageSpy,
            UserEventType::DeviceConnectFailed
        ) >= 1,
        8'000
    );
    QTest::qWait(250);
    QCOMPARE(
        countMessages(messageSpy, UserEventType::DeviceConnectFailed),
        1
    );
    const QString failureText = firstMessageText(
        messageSpy,
        UserEventType::DeviceConnectFailed
    );
    const QStringList forbidden {
        QStringLiteral("RTMP"),
        QStringLiteral("TCP"),
        QStringLiteral("FFmpeg"),
        QStringLiteral("1935"),
        QStringLiteral("-110")
    };
    for (const QString &term : forbidden) {
        QVERIFY(!failureText.contains(term, Qt::CaseInsensitive));
    }

    VideoWidget *videoWidget = window.primaryVideoWidget();
    QVERIFY(videoWidget != nullptr);
    QVERIFY(QMetaObject::invokeMethod(
        videoWidget,
        "reconnectRequested",
        Qt::DirectConnection,
        Q_ARG(VideoWidget *, videoWidget)
    ));
    QTRY_COMPARE_WITH_TIMEOUT(
        countMessages(
            messageSpy,
            UserEventType::ManualReconnectStarted
        ),
        1,
        2'000
    );

    QVERIFY(controller.removeConnection(streamId, false));
    QCOMPARE(countMessages(messageSpy, UserEventType::DeviceRemoved), 1);
    playbackManager.stopAll();

    const QString systemPath = logManager.systemLogFilePath();
    const QString auditPath = logManager.auditLogFilePath();
    logManager.shutdown();

    QFile systemFile(systemPath);
    QVERIFY(systemFile.open(QIODevice::ReadOnly));
    const QByteArray systemPayload = systemFile.readAll();
    QVERIFY(systemPayload.contains("\"event\":\"stream_error\""));
    QVERIFY(systemPayload.contains("\"nativeErrorCode\""));
    QVERIFY(!systemPayload.contains("private-key"));
    QVERIFY(!systemPayload.contains("token=x"));

    QFile auditFile(auditPath);
    QVERIFY(auditFile.open(QIODevice::ReadOnly));
    const QByteArray auditPayload = auditFile.readAll();
    QVERIFY(auditPayload.contains("\"action\":\"ADD_DEVICE\""));
    QVERIFY(auditPayload.contains("\"action\":\"REMOVE_DEVICE\""));
    QVERIFY(auditPayload.contains("\"action\":\"MANUAL_RECONNECT\""));
    QVERIFY(auditPayload.contains("\"result\":\"SUCCESS\""));
    QVERIFY(auditPayload.contains("\"result\":\"FAILURE\""));
    QVERIFY(!auditPayload.contains("private-key"));
    QVERIFY(!auditPayload.contains("token=x"));
}

void StreamConnectionControllerTest::
profileConnectionGeneratesUrlAndValidates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LoggingOptions loggingOptions;
    loggingOptions.directoryPath = directory.path();
    loggingOptions.minimumLevel = LogLevel::Trace;
    loggingOptions.consoleEnabled = false;

    LogManager logManager;
    QVERIFY(logManager.initialize(loggingOptions));
    UserMessageService userMessages(60'000);

    PlaybackPerformanceOptions playbackOptions;
    playbackOptions.decodeWorkerCount = 1;
    playbackOptions.reconnectDelayMs = 50;
    playbackOptions.maximumConsecutiveFailures = 2;
    MultiStreamPlaybackManager playbackManager(playbackOptions);
    MainWindow window;
    window.setUserMessageService(&userMessages);
    StreamConnectionController controller(
        &window,
        &playbackManager,
        &logManager,
        &userMessages
    );

    MediaServerEndpoint endpoint;
    endpoint.host = QStringLiteral("192.168.50.2");
    endpoint.rtmpPort = 1936;
    endpoint.application = QStringLiteral("hd");

    CameraStreamProfile profile;
    profile.cameraId = QStringLiteral("camera01");
    profile.displayName = QStringLiteral("摄像头 01");
    profile.streamKey = QStringLiteral("camera01");

    // startImmediately=false：本用例只验证绑定与 URL，不发起网络连接。
    const StreamId streamId =
        controller.addConnection(profile, endpoint, false);
    QVERIFY(streamId != kInvalidStreamId);

    // 用完整 URL 手工接入同一路被拒，证明 profile 生成的 URL 与预期一致。
    QCOMPARE(
        controller.addConnection(
            QStringLiteral("另一台"),
            QStringLiteral("rtmp://192.168.50.2:1936/hd/camera01"),
            false,
            false
        ),
        kInvalidStreamId
    );

    // 非法 streamKey 无法生成 URL，必须拒绝。
    CameraStreamProfile invalidKeyProfile;
    invalidKeyProfile.cameraId = QStringLiteral("camera02");
    invalidKeyProfile.displayName = QStringLiteral("摄像头 02");
    invalidKeyProfile.streamKey = QStringLiteral("bad/key");
    QCOMPARE(
        controller.addConnection(invalidKeyProfile, endpoint, false),
        kInvalidStreamId
    );

    // cameraId 重复时即使 streamKey 不同也必须拒绝。
    CameraStreamProfile duplicateIdProfile;
    duplicateIdProfile.cameraId = QStringLiteral("camera01");
    duplicateIdProfile.displayName = QStringLiteral("另一台摄像头");
    duplicateIdProfile.streamKey = QStringLiteral("camera02");
    QCOMPARE(
        controller.addConnection(duplicateIdProfile, endpoint, false),
        kInvalidStreamId
    );

    playbackManager.stopAll();
    logManager.shutdown();
}

void StreamConnectionControllerTest::profileConnectionsRespectCapacity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LoggingOptions loggingOptions;
    loggingOptions.directoryPath = directory.path();
    loggingOptions.minimumLevel = LogLevel::Trace;
    loggingOptions.consoleEnabled = false;

    LogManager logManager;
    QVERIFY(logManager.initialize(loggingOptions));
    UserMessageService userMessages(60'000);

    PlaybackPerformanceOptions playbackOptions;
    playbackOptions.decodeWorkerCount = 1;
    playbackOptions.reconnectDelayMs = 50;
    playbackOptions.maximumConsecutiveFailures = 2;
    MultiStreamPlaybackManager playbackManager(playbackOptions);
    MainWindow window;
    window.setUserMessageService(&userMessages);
    StreamConnectionController controller(
        &window,
        &playbackManager,
        &logManager,
        &userMessages
    );

    const MediaServerEndpoint endpoint;
    for (int index = 1; index <= 16; ++index) {
        CameraStreamProfile profile;
        const QString name = QStringLiteral("camera%1")
                                 .arg(index, 2, 10, QLatin1Char('0'));
        profile.cameraId = name;
        profile.displayName = name;
        profile.streamKey = name;
        QVERIFY2(
            controller.addConnection(profile, endpoint, false) !=
                kInvalidStreamId,
            qPrintable(name)
        );
    }

    CameraStreamProfile overflow;
    overflow.cameraId = QStringLiteral("camera17");
    overflow.displayName = QStringLiteral("camera17");
    overflow.streamKey = QStringLiteral("camera17");
    QCOMPARE(
        controller.addConnection(overflow, endpoint, false),
        kInvalidStreamId
    );

    playbackManager.stopAll();
    logManager.shutdown();
}

void StreamConnectionControllerTest::selectsStableControlTargetAndTracksPresence()
{
    QCOMPARE(StreamConnectionController::deviceIdFromRtmpUrl(
                 QStringLiteral("rtmp://127.0.0.1:1935/live/040001")),
             QStringLiteral("040001"));
    QVERIFY(StreamConnectionController::deviceIdFromRtmpUrl(
                QStringLiteral("rtmp://127.0.0.1:1935/live/bad%2Fid"))
                .isEmpty());

    QTemporaryDir directory;
    LoggingOptions loggingOptions;
    loggingOptions.directoryPath = directory.path();
    loggingOptions.consoleEnabled = false;
    LogManager logManager;
    QVERIFY(logManager.initialize(loggingOptions));
    UserMessageService userMessages(60'000);
    PlaybackPerformanceOptions playbackOptions;
    playbackOptions.decodeWorkerCount = 1;
    MultiStreamPlaybackManager playbackManager(playbackOptions);
    MainWindow window;
    StreamConnectionController controller(
        &window, &playbackManager, &logManager, &userMessages);
    QSignalSpy targetSpy(&controller,
                         &StreamConnectionController::controlTargetChanged);

    const StreamId firstId = controller.addConnection(
        QStringLiteral("First"),
        QStringLiteral("rtmp://127.0.0.1:1935/live/040001"), false);
    QVERIFY(firstId != kInvalidStreamId);
    QCOMPARE(controller.selectedControlStreamId(), firstId);
    VideoWidget *first = window.videoWidgetAt(0);
    QVERIFY(first != nullptr && first->isControlTargetSelected());
    QCOMPARE(targetSpy.count(), 1);

    const StreamId secondId = controller.addConnection(
        QStringLiteral("Second"),
        QStringLiteral("rtmp://127.0.0.1:1935/live/040002"), false);
    QVERIFY(secondId != kInvalidStreamId);
    VideoWidget *second = window.videoWidgetAt(1);
    QVERIFY(second != nullptr && !second->isControlTargetSelected());
    QVERIFY(QMetaObject::invokeMethod(
        second, "controlTargetRequested", Qt::DirectConnection,
        Q_ARG(VideoWidget *, second)));
    QCOMPARE(controller.selectedControlStreamId(), secondId);
    QVERIFY(!first->isControlTargetSelected());
    QVERIFY(second->isControlTargetSelected());

    controller.setDevicePresence(QStringLiteral("040002"),
                                 DevicePresenceState::Online);
    QCOMPARE(second->devicePresenceState(), DevicePresenceState::Online);
    QVERIFY(controller.removeConnection(secondId, false));
    QCOMPARE(controller.selectedControlStreamId(), kInvalidStreamId);
    QCOMPARE(targetSpy.constLast().at(0).value<StreamId>(), kInvalidStreamId);

    playbackManager.stopAll();
    logManager.shutdown();
}

QTEST_MAIN(StreamConnectionControllerTest)

#include "StreamConnectionControllerTest.moc"
