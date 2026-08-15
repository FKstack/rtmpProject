#include <QDockWidget>
#include <QPushButton>
#include <QTest>
#include <QTextEdit>
#include <QToolButton>

#include "media/PlaybackTypes.h"
#include "ui/LogPanel.h"
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

namespace {

UserMessage makeMessage(int index)
{
    return {
        QDateTime::currentDateTimeUtc(),
        UserEventType::DeviceConnected,
        UserMessageKind::Success,
        static_cast<std::uint64_t>(index),
        QStringLiteral("摄像头 %1 连接成功").arg(index)
    };
}

} // namespace

class LogPanelTest final : public QObject
{
    Q_OBJECT

private slots:
    void panelShowsOnlyUserMessagesAndClears();
    void panelKeepsBoundedEntries();
    void mainWindowMapsDeviceStatusWithoutTechnicalDetails();
};

void LogPanelTest::panelShowsOnlyUserMessagesAndClears()
{
    LogPanel panel;
    panel.appendMessage(makeMessage(1));
    QCOMPARE(panel.entryCount(), 1);
    const QString text = panel.textEdit()->toPlainText();
    QVERIFY(text.contains(QStringLiteral("摄像头 1 连接成功")));
    QVERIFY(!text.contains(QStringLiteral("DEBUG")));
    QVERIFY(!text.contains(QStringLiteral("event")));
    QVERIFY(
        panel.findChild<QWidget *>(
            QStringLiteral("logLevelComboBox")
        ) == nullptr
    );

    auto *pause = panel.findChild<QToolButton *>(
        QStringLiteral("pauseLogScrollButton")
    );
    QVERIFY(pause != nullptr);
    pause->click();
    QVERIFY(panel.isAutoScrollPaused());

    auto *clear = panel.findChild<QPushButton *>(
        QStringLiteral("clearLogButton")
    );
    QVERIFY(clear != nullptr);
    clear->click();
    QCOMPARE(panel.entryCount(), 0);
    QVERIFY(panel.textEdit()->toPlainText().isEmpty());
}

void LogPanelTest::panelKeepsBoundedEntries()
{
    LogPanel panel;
    for (int index = 0; index < 5'010; ++index) {
        panel.appendMessage(makeMessage(index));
    }
    QCOMPARE(panel.entryCount(), 5'000);
    QVERIFY(!panel.textEdit()->toPlainText().contains(
        QStringLiteral("摄像头 0 连接成功")
    ));
    QVERIFY(panel.textEdit()->toPlainText().contains(
        QStringLiteral("摄像头 5009 连接成功")
    ));
}

void LogPanelTest::
mainWindowMapsDeviceStatusWithoutTechnicalDetails()
{
    MainWindow window;
    QVERIFY(window.logDockWidget() != nullptr);
    QCOMPARE(window.logDockWidget()->windowTitle(), QStringLiteral("运行消息"));
    QVERIFY(window.logPanel() != nullptr);

    VideoWidget *videoWidget =
        window.addConnectionWidget(QStringLiteral("Camera 01"));
    QVERIFY(videoWidget != nullptr);
    window.updateDeviceStatus(videoWidget, DeviceStatus::Connecting);
    QCOMPARE(videoWidget->statusText(), QStringLiteral("正在连接..."));
    window.updateDeviceStatus(videoWidget, DeviceStatus::Reconnecting);
    QCOMPARE(videoWidget->statusText(), QStringLiteral("连接中断，正在重连..."));
    window.updateDeviceStatus(
        videoWidget,
        DeviceStatus::Error,
        UserFailureReason::MediaUnavailable
    );
    QCOMPARE(
        videoWidget->statusText(),
        QStringLiteral("暂时无法获取设备画面")
    );
    QVERIFY(!videoWidget->statusText().contains(QStringLiteral("FFmpeg")));
    window.updateDeviceStatus(videoWidget, DeviceStatus::Disconnected);
    QCOMPARE(videoWidget->statusText(), QStringLiteral("已断开"));
}

QTEST_MAIN(LogPanelTest)

#include "LogPanelTest.moc"
