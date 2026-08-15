#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "event_center/EventCenterService.h"

namespace {

EventObservation fault(SecurityEventType type, const QString &resource)
{
    EventObservation result;
    result.eventType = type;
    result.localResourceId = resource;
    result.severity = SecurityEventSeverity::Medium;
    result.source = QStringLiteral("test-source");
    return result;
}

SecurityEventRecord closedEvent(int index, const QDateTime &closedAt,
                                bool withEvidence = false)
{
    SecurityEventRecord event;
    event.eventId = QStringLiteral("closed-%1").arg(index);
    event.eventType = SecurityEventType::ManualIncident;
    event.state = SecurityEventState::Closed;
    event.localResourceId = QStringLiteral("resource:%1").arg(index);
    event.openedAtUtc = closedAt.addSecs(-60);
    event.lastObservedAtUtc = event.openedAtUtc;
    event.resolvedAtUtc = closedAt.addSecs(-10);
    event.closedAtUtc = closedAt;
    event.resolutionSource = ResolutionSource::Operator;
    event.closeDisposition = CloseDisposition::ObservedRecovery;
    if (withEvidence) event.evidenceIds.append(QStringLiteral("evidence-1"));
    return event;
}

} // namespace

class EventCenterServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void systemLifecycleDeduplicatesRecoversAndRecurs();
    void manualAndForcedCloseRulesAreStrict();
    void persistsUnicodeAndProtectsUnreadableSchemas();
    void prunesClosedEventsAndCreatesEvidenceTombstone();
    void failedCommitDoesNotPublishCandidate();
};

void EventCenterServiceTest::systemLifecycleDeduplicatesRecoversAndRecurs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDateTime now = QDateTime::fromString(
        QStringLiteral("2026-08-15T01:00:00.000Z"), Qt::ISODateWithMs);
    EventCenterService service(
        directory.filePath(QStringLiteral("events-v1.json")), [&] { return now; });
    QVERIFY(service.initialize());

    EventObservation observation = fault(
        SecurityEventType::VideoStreamLost, QStringLiteral("camera:front"));
    const auto created = service.observeFault(observation);
    QVERIFY(created.succeeded());
    QVERIFY(created.changed);
    QCOMPARE(service.events().size(), 1);
    QCOMPARE(service.events().first().occurrenceCount, quint64 {1});
    QCOMPARE(service.events().first().history.size(), 1);

    now = now.addSecs(2);
    QVERIFY(service.observeFault(observation).succeeded());
    QCOMPARE(service.events().first().occurrenceCount, quint64 {2});
    QCOMPARE(service.events().first().eventRevision, quint64 {2});
    QCOMPARE(service.events().first().history.size(), 1);

    QVERIFY(service.acknowledge(created.eventId, QStringLiteral("operator")).succeeded());
    QCOMPARE(service.events().first().state, SecurityEventState::Acknowledged);
    QVERIFY(!service.resolveManualIncident(
        created.eventId, QStringLiteral("operator")).succeeded());

    now = now.addSecs(2);
    QVERIFY(service.observeRecovery(observation).succeeded());
    QCOMPARE(service.events().first().state, SecurityEventState::Resolved);
    QCOMPARE(service.events().first().resolutionSource,
             ResolutionSource::PlatformObservation);

    now = now.addSecs(2);
    QVERIFY(service.observeFault(observation).succeeded());
    QCOMPARE(service.events().first().state, SecurityEventState::Open);
    QCOMPARE(service.events().first().occurrenceCount, quint64 {3});
    QCOMPARE(service.events().first().history.last().transition,
             EventTransitionKind::Recurred);

    QVERIFY(service.observeRecovery(observation).succeeded());
    QVERIFY(service.closeResolved(created.eventId, QStringLiteral("operator")).succeeded());
    QCOMPARE(service.events().first().state, SecurityEventState::Closed);

    now = now.addSecs(2);
    const auto reopened = service.observeFault(observation);
    QVERIFY(reopened.succeeded());
    QVERIFY(reopened.eventId != created.eventId);
    QCOMPARE(service.events().size(), 2);
}

void EventCenterServiceTest::manualAndForcedCloseRulesAreStrict()
{
    QTemporaryDir directory;
    QDateTime now = QDateTime::currentDateTimeUtc();
    EventCenterService service(directory.filePath(QStringLiteral("events.json")),
                               [&] { return now; });
    QVERIFY(service.initialize());
    const auto manual = service.createManualIncident(
        SecurityEventSeverity::High, QStringLiteral("platform:local"), {},
        QStringLiteral("本机平台"), QStringLiteral("local"),
        QStringLiteral("操作者看到现场异常"), QStringLiteral("tester"));
    QVERIFY(manual.succeeded());
    QVERIFY(!service.resolveManualIncident(
        manual.eventId, QStringLiteral("tester")).succeeded());
    QVERIFY(service.acknowledge(manual.eventId, QStringLiteral("tester")).succeeded());
    QVERIFY(service.resolveManualIncident(
        manual.eventId, QStringLiteral("tester")).succeeded());
    QVERIFY(service.closeResolved(manual.eventId, QStringLiteral("tester")).succeeded());

    const auto system = service.observeFault(fault(
        SecurityEventType::MqttConnectionLost,
        QStringLiteral("transport:mqtt-control")));
    QVERIFY(system.succeeded());
    QVERIFY(!service.closeWithoutObservedRecovery(
        system.eventId, {}, QStringLiteral("tester")).succeeded());
    QVERIFY(service.closeWithoutObservedRecovery(
        system.eventId, QStringLiteral("设备已离场，无法再观察恢复"),
        QStringLiteral("tester")).succeeded());
    const SecurityEventRecord closed = service.events().last();
    QCOMPARE(closed.state, SecurityEventState::Closed);
    QCOMPARE(closed.closeDisposition,
             CloseDisposition::ClosedWithoutObservedRecovery);
    QVERIFY(!closed.resolvedAtUtc.isValid());
    QVERIFY(closed.history.last().note.contains(QStringLiteral("设备已离场")));
}

void EventCenterServiceTest::persistsUnicodeAndProtectsUnreadableSchemas()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("events.json"));
    {
        EventCenterService service(path);
        QVERIFY(service.initialize());
        QVERIFY(service.createManualIncident(
            SecurityEventSeverity::Medium, QStringLiteral("platform:local"), {},
            QStringLiteral("值守平台"), QStringLiteral("local"),
            QStringLiteral("中文事件说明"), QStringLiteral("本机用户")).succeeded());
    }
    {
        EventCenterService service(path);
        QVERIFY(service.initialize());
        QCOMPARE(service.events().size(), 1);
        QCOMPARE(service.events().first().note, QStringLiteral("中文事件说明"));
    }

    const QString corruptPath = directory.filePath(QStringLiteral("corrupt.json"));
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QCOMPARE(corrupt.write("{broken"), qint64 {7});
    corrupt.close();
    EventCenterService corruptService(corruptPath);
    QVERIFY(!corruptService.initialize());
    QVERIFY(!corruptService.isWriteEnabled());
    QFile preserved(corruptPath);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArray("{broken"));

    const QString higherPath = directory.filePath(QStringLiteral("higher.json"));
    QFile higher(higherPath);
    QVERIFY(higher.open(QIODevice::WriteOnly));
    higher.write(QJsonDocument(QJsonObject {
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("events"), QJsonArray {}},
        {QStringLiteral("tombstones"), QJsonArray {}},
    }).toJson());
    higher.close();
    EventCenterService higherService(higherPath);
    QVERIFY(!higherService.initialize());
    QVERIFY(higherService.storageError().contains(QStringLiteral("高于")));
}

void EventCenterServiceTest::prunesClosedEventsAndCreatesEvidenceTombstone()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("events.json"));
    const QDateTime now = QDateTime::fromString(
        QStringLiteral("2026-08-15T02:00:00.000Z"), Qt::ISODateWithMs);
    QList<SecurityEventRecord> seed;
    for (int index = 0; index < 5'001; ++index)
        seed.append(closedEvent(index, now.addSecs(-index)));
    seed.append(closedEvent(6'000, now.addDays(-181), true));
    EventCenterStore store(path);
    QString error;
    QVERIFY2(store.save(seed, {}, now, &error), qPrintable(error));

    EventCenterService service(path, [now] { return now; });
    QVERIFY(service.initialize());
    QVERIFY(service.observeFault(fault(
        SecurityEventType::MqttConnectionLost,
        QStringLiteral("transport:mqtt-control"))).succeeded());
    int closedCount = 0;
    for (const auto &event : service.events())
        if (event.state == SecurityEventState::Closed) ++closedCount;
    QCOMPARE(closedCount, 5'000);
    QCOMPARE(service.tombstones().size(), 1);
    QCOMPARE(service.tombstones().first().eventId, QStringLiteral("closed-6000"));
}

void EventCenterServiceTest::failedCommitDoesNotPublishCandidate()
{
    QTemporaryDir directory;
    const QString parentFile = directory.filePath(QStringLiteral("not-a-dir"));
    QFile file(parentFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("occupied");
    file.close();
    EventCenterService service(parentFile + QStringLiteral("/events.json"));
    QVERIFY(service.initialize());
    const auto result = service.observeFault(fault(
        SecurityEventType::MqttConnectionLost,
        QStringLiteral("transport:mqtt-control")));
    QVERIFY(!result.succeeded());
    QVERIFY(!service.isWriteEnabled());
    QVERIFY(service.events().isEmpty());
}

QTEST_APPLESS_MAIN(EventCenterServiceTest)
#include "EventCenterServiceTest.moc"
