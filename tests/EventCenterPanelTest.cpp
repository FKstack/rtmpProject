#include <QDockWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>
#include <QSignalSpy>

#include "ui/EventCenterPanel.h"
#include "ui/EventDetailDialog.h"
#include "ui/MainWindow.h"

namespace {

SecurityEventRecord eventRecord(SecurityEventState state,
                                SecurityEventType type,
                                SecurityEventSeverity severity,
                                const QString &id)
{
    SecurityEventRecord event;
    event.eventId = id;
    event.eventType = type;
    event.severity = severity;
    event.state = state;
    event.localResourceId = QStringLiteral("device:vehicle-01");
    event.displayNameSnapshot = QStringLiteral("测试车辆");
    event.openedAtUtc = QDateTime::currentDateTimeUtc();
    event.lastObservedAtUtc = event.openedAtUtc;
    if (state == SecurityEventState::Closed)
        event.closedAtUtc = event.openedAtUtc.addSecs(2);
    return event;
}

} // namespace

class EventCenterPanelTest final : public QObject
{
    Q_OBJECT

private slots:
    void filtersAndEnablesOnlyLegalActions();
    void storageFailureDisablesMutations();
    void mainWindowKeepsDockHiddenAndBadgeVisible();
    void detailDialogGatesCaptureAndStatesNoHashVerification();
};

void EventCenterPanelTest::filtersAndEnablesOnlyLegalActions()
{
    EventCenterPanel panel;
    const auto open = eventRecord(
        SecurityEventState::Open, SecurityEventType::MqttConnectionLost,
        SecurityEventSeverity::Critical, QStringLiteral("open"));
    const auto closed = eventRecord(
        SecurityEventState::Closed, SecurityEventType::ManualIncident,
        SecurityEventSeverity::Low, QStringLiteral("closed"));
    panel.setEvents({closed, open}, {1, SecurityEventSeverity::Critical});
    auto *table = panel.findChild<QTableWidget *>(
        QStringLiteral("eventCenterTable"));
    auto *ack = panel.findChild<QPushButton *>(
        QStringLiteral("acknowledgeEventButton"));
    auto *forceClose = panel.findChild<QPushButton *>(
        QStringLiteral("forceCloseEventButton"));
    QVERIFY(table != nullptr);
    QVERIFY(ack != nullptr);
    QVERIFY(forceClose != nullptr);
    QCOMPARE(table->rowCount(), 1);
    table->selectRow(0);
    QVERIFY(ack->isEnabled());
    QVERIFY(forceClose->isEnabled());
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("待处理"));

    auto *filter = panel.findChild<QComboBox *>(
        QStringLiteral("eventFilterCombo"));
    QVERIFY(filter != nullptr);
    filter->setCurrentIndex(1);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("已关闭"));
}

void EventCenterPanelTest::storageFailureDisablesMutations()
{
    EventCenterPanel panel;
    panel.setEvents({eventRecord(
        SecurityEventState::Open, SecurityEventType::MqttConnectionLost,
        SecurityEventSeverity::High, QStringLiteral("open"))},
        {1, SecurityEventSeverity::High});
    auto *table = panel.findChild<QTableWidget *>(
        QStringLiteral("eventCenterTable"));
    table->selectRow(0);
    panel.setStorageState(false, QStringLiteral("测试写入失败"));
    QVERIFY(!panel.findChild<QPushButton *>(
        QStringLiteral("acknowledgeEventButton"))->isEnabled());
    QVERIFY(!panel.findChild<QPushButton *>(
        QStringLiteral("createManualIncidentButton"))->isEnabled());
    auto *banner = panel.findChild<QLabel *>(
        QStringLiteral("eventStorageBanner"));
    QVERIFY(banner->isVisibleTo(&panel) || !panel.isVisible());
    QVERIFY(banner->text().contains(QStringLiteral("测试写入失败")));
}

void EventCenterPanelTest::mainWindowKeepsDockHiddenAndBadgeVisible()
{
    MainWindow window;
    auto *panel = new EventCenterPanel(&window);
    window.installEventCenterPanel(panel);
    auto *dock = window.findChild<QDockWidget *>(
        QStringLiteral("eventCenterDockWidget"));
    auto *badge = window.findChild<QToolButton *>(
        QStringLiteral("eventCenterStatusBadge"));
    QVERIFY(dock != nullptr);
    QVERIFY(badge != nullptr);
    QVERIFY(dock->isHidden());
    window.setEventCenterSummary({2, SecurityEventSeverity::Critical}, true);
    QVERIFY(badge->text().contains(QStringLiteral("2")));
    QCOMPARE(badge->property("severity").toString(), QStringLiteral("critical"));
    badge->click();
    QVERIFY(!dock->isHidden());
    window.setEventCenterSummary({}, false);
    QCOMPARE(badge->text(), QStringLiteral("事件存储不可写"));
}

void EventCenterPanelTest::detailDialogGatesCaptureAndStatesNoHashVerification()
{
    EventCenterPanel panel;
    auto event = eventRecord(
        SecurityEventState::Open, SecurityEventType::VideoStreamLost,
        SecurityEventSeverity::High, QStringLiteral("event-detail"));
    event.localResourceId = QStringLiteral("camera:front");
    event.deviceId = QStringLiteral("vehicle-01");
    panel.setEvents({event}, {1, SecurityEventSeverity::High});
    panel.setEvidenceStorageState(true, {});
    panel.setCaptureResources({{
        QStringLiteral("camera:front"), QStringLiteral("vehicle-01"),
        QStringLiteral("前置摄像头"), QStringLiteral("camera-profile")}});
    auto *table = panel.findChild<QTableWidget *>(
        QStringLiteral("eventCenterTable"));
    table->selectRow(0);
    QSignalSpy captureSpy(&panel, &EventCenterPanel::captureEvidenceRequested);
    panel.findChild<QPushButton *>(QStringLiteral("eventDetailButton"))->click();
    QTRY_VERIFY(panel.findChild<EventDetailDialog *>() != nullptr);
    auto *dialog = panel.findChild<EventDetailDialog *>();
    auto *capture = dialog->findChild<QPushButton *>(
        QStringLiteral("captureEvidenceButton"));
    auto *notice = dialog->findChild<QLabel *>(
        QStringLiteral("evidenceHonestyNotice"));
    QVERIFY(capture->isEnabled());
    QVERIFY(notice->text().contains(QStringLiteral("不进行内容哈希校验")));
    capture->click();
    QCOMPARE(captureSpy.count(), 1);
    QCOMPARE(captureSpy.first().at(0).toString(), QStringLiteral("event-detail"));
    QCOMPARE(captureSpy.first().at(1).toString(), QStringLiteral("camera:front"));
    dialog->close();
}

QTEST_MAIN(EventCenterPanelTest)
#include "EventCenterPanelTest.moc"
