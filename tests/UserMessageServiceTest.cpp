#include <QSignalSpy>
#include <QTest>

#include "logging/UserMessageService.h"

class UserMessageServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void mapsEventsToPlainLanguage();
    void suppressesRepeatedFailuresAndResetsAfterRecovery();
    void supportsFutureLoginEvents();
};

void UserMessageServiceTest::mapsEventsToPlainLanguage()
{
    UserMessageService service;
    QSignalSpy spy(&service, &UserMessageService::messageAdded);
    service.publish({
        UserEventType::DeviceConnectFailed,
        UserFailureReason::HostUnavailable,
        3,
        QStringLiteral("摄像头 03")
    });
    QCOMPARE(spy.count(), 1);
    const UserMessage message =
        spy.constFirst().constFirst().value<UserMessage>();
    QVERIFY(message.text.contains(QStringLiteral("同一 Wi-Fi")));
    const QStringList forbidden {
        QStringLiteral("RTMP"),
        QStringLiteral("TCP"),
        QStringLiteral("FFmpeg"),
        QStringLiteral("Socket"),
        QStringLiteral("-110"),
        QStringLiteral("1935")
    };
    for (const QString &term : forbidden) {
        QVERIFY(!message.text.contains(term, Qt::CaseInsensitive));
    }

    QCOMPARE(
        UserMessageService::messageText({
            UserEventType::DeviceAddFailed,
            UserFailureReason::DuplicateDevice,
            0,
            QStringLiteral("摄像头 01")
        }),
        QStringLiteral("该设备已经添加")
    );
}

void UserMessageServiceTest::
suppressesRepeatedFailuresAndResetsAfterRecovery()
{
    UserMessageService service(60'000);
    QSignalSpy spy(&service, &UserMessageService::messageAdded);
    const UserEvent failure {
        UserEventType::DeviceConnectFailed,
        UserFailureReason::ConnectionTimeout,
        1,
        QStringLiteral("摄像头 01")
    };
    service.publish(failure);
    service.publish(failure);
    service.publish(failure);
    QCOMPARE(spy.count(), 1);

    service.publish({
        UserEventType::DeviceConnected,
        UserFailureReason::None,
        1,
        QStringLiteral("摄像头 01")
    });
    QCOMPARE(spy.count(), 2);
    service.publish(failure);
    QCOMPARE(spy.count(), 3);
}

void UserMessageServiceTest::supportsFutureLoginEvents()
{
    UserMessageService service;
    QSignalSpy spy(&service, &UserMessageService::messageAdded);
    service.publish({
        UserEventType::LoginFailed,
        UserFailureReason::AuthenticationFailed,
        0,
        {}
    });
    QCOMPARE(spy.count(), 1);
    QCOMPARE(
        spy.constFirst()
            .constFirst()
            .value<UserMessage>()
            .text,
        QStringLiteral("登录失败，请检查用户名和密码")
    );
}

QTEST_GUILESS_MAIN(UserMessageServiceTest)

#include "UserMessageServiceTest.moc"
