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

QTEST_MAIN(StreamConnectionControllerTest)

#include "StreamConnectionControllerTest.moc"
