#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>

#include "evidence/EvidenceService.h"

namespace {

EvidenceStorageSpace ampleSpace(const QString &)
{
    return {20LL * 1024 * 1024 * 1024, 100LL * 1024 * 1024 * 1024};
}

EvidenceCaptureRequest requestFor(const QString &eventId, qint64 frameAge)
{
    EvidenceCaptureRequest request;
    request.eventId = eventId;
    request.deviceId = QStringLiteral("vehicle-01");
    request.streamId = 42;
    request.requestedAtUtc = QDateTime::currentDateTimeUtc();
    request.capturedAtUtc = request.requestedAtUtc;
    request.frameFreshnessMs = frameAge;
    request.sourcePlaybackState = QStringLiteral("playing");
    request.playbackPlaying = true;
    request.image = QImage(64, 32, QImage::Format_RGB32);
    request.image.fill(Qt::red);
    request.actor = QStringLiteral("tester");
    return request;
}

} // namespace

class EvidenceServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void capturesAtomicallyAndRejectsInvalidFrames();
    void marksMissingFilesOnRestart();
    void exportsDirectoryWithoutHashVerification();
    void rejectsLowDiskAndBoundsQueue();
    void rejectsCatalogPathTraversalOnRecovery();
};

void EvidenceServiceTest::capturesAtomicallyAndRejectsInvalidFrames()
{
    QTemporaryDir directory;
    EvidenceService service(directory.path(), {}, ampleSpace);
    QVERIFY(service.initialize());
    QSignalSpy completed(&service, &EvidenceService::captureCompleted);
    QSignalSpy failed(&service, &EvidenceService::captureFailed);

    const auto firstCapture = service.capture(
        requestFor(QStringLiteral("event-1"), 1000));
    QVERIFY2(firstCapture.accepted, qPrintable(firstCapture.message));
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
    QCOMPARE(service.records().size(), 1);
    QCOMPARE(service.captureAttempts().size(), 1);
    const EvidenceRecord record = service.records().first();
    QVERIFY(record.sizeBytes > 0);
    QVERIFY(QFileInfo::exists(QDir(directory.path()).filePath(record.relativePath)));

    const auto stale = service.capture(requestFor(QStringLiteral("event-1"), 1001));
    QVERIFY(!stale.accepted);
    QCOMPARE(stale.failure, EvidenceCaptureFailure::FrameStale);
    QCOMPARE(failed.count(), 1);

    auto notPlaying = requestFor(QStringLiteral("event-1"), 0);
    notPlaying.playbackPlaying = false;
    const auto rejected = service.capture(notPlaying);
    QVERIFY(!rejected.accepted);
    QCOMPARE(rejected.failure, EvidenceCaptureFailure::PlaybackNotPlaying);
    QCOMPARE(service.captureAttempts().size(), 3);

    QFile catalog(directory.filePath(QStringLiteral("catalog-v1.json")));
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    QVERIFY(!catalog.readAll().toLower().contains("sha256"));
}

void EvidenceServiceTest::marksMissingFilesOnRestart()
{
    QTemporaryDir directory;
    QString objectPath;
    {
        EvidenceService service(directory.path(), {}, ampleSpace);
        QVERIFY(service.initialize());
        QSignalSpy completed(&service, &EvidenceService::captureCompleted);
        const auto capture = service.capture(requestFor(QStringLiteral("event-2"), 0));
        QVERIFY2(capture.accepted, qPrintable(capture.message));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        objectPath = QDir(directory.path()).filePath(
            service.records().first().relativePath);
    }
    QVERIFY(QFile::remove(objectPath));
    EvidenceService restored(directory.path(), {}, ampleSpace);
    QVERIFY(restored.initialize());
    QCOMPARE(restored.records().size(), 1);
    QCOMPARE(restored.records().first().availability,
             EvidenceAvailability::Missing);
}

void EvidenceServiceTest::exportsDirectoryWithoutHashVerification()
{
    QTemporaryDir directory;
    EvidenceService service(directory.filePath(QStringLiteral("store")), {}, ampleSpace);
    QVERIFY(service.initialize());
    QSignalSpy captureSpy(&service, &EvidenceService::captureCompleted);
    const auto capture = service.capture(requestFor(QStringLiteral("event-3"), 0));
    QVERIFY2(capture.accepted, qPrintable(capture.message));
    QTRY_COMPARE_WITH_TIMEOUT(captureSpy.count(), 1, 5000);

    const QString exports = directory.filePath(QStringLiteral("exports"));
    QVERIFY(QDir().mkpath(exports));
    IncidentExportRequest request;
    request.eventId = QStringLiteral("event-3");
    request.eventRevision = 4;
    request.destinationParentDirectory = exports;
    request.actor = QStringLiteral("tester");
    request.eventSnapshot = {{QStringLiteral("eventId"), request.eventId}};
    request.stateHistory.append(QJsonObject {
        {QStringLiteral("transition"), QStringLiteral("created")}});
    request.linkedControlAttempts.append(QJsonObject {
        {QStringLiteral("attemptId"), QStringLiteral("attempt-1")},
        {QStringLiteral("executionConfirmation"), QStringLiteral("unavailable")}});
    QSignalSpy exportSpy(&service, &EvidenceService::exportCompleted);
    QVERIFY(service.exportIncident(request).accepted);
    QTRY_COMPARE_WITH_TIMEOUT(exportSpy.count(), 1, 5000);
    const EvidenceExportResult result =
        qvariant_cast<EvidenceExportResult>(exportSpy.first().first());
    QVERIFY2(result.succeeded, qPrintable(result.message));
    QVERIFY(QFileInfo::exists(QDir(result.outputDirectory).filePath(
        QStringLiteral("manifest.json"))));
    QFile manifest(QDir(result.outputDirectory).filePath(
        QStringLiteral("manifest.json")));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const QByteArray payload = manifest.readAll();
    QVERIFY(payload.contains("\"contentHashVerification\": \"not_performed\""));
    QVERIFY(!payload.toLower().contains("sha256"));
    QFile controls(QDir(result.outputDirectory).filePath(
        QStringLiteral("audit/control-attempts.jsonl")));
    QVERIFY(controls.open(QIODevice::ReadOnly));
    QVERIFY(controls.readAll().contains(
        "\"executionConfirmation\":\"unavailable\""));
}

void EvidenceServiceTest::rejectsLowDiskAndBoundsQueue()
{
    QTemporaryDir lowDirectory;
    EvidenceService lowSpaceService(
        lowDirectory.path(), {},
        [](const QString &) {
            return EvidenceStorageSpace {
                1024LL * 1024 * 1024,
                100LL * 1024 * 1024 * 1024};
        });
    QVERIFY(lowSpaceService.initialize());
    QSignalSpy lowFault(&lowSpaceService,
                        &EvidenceService::subsystemFaultObserved);
    const auto low = lowSpaceService.capture(
        requestFor(QStringLiteral("event-low"), 0));
    QVERIFY(!low.accepted);
    QCOMPARE(low.failure, EvidenceCaptureFailure::StorageLow);
    QCOMPARE(lowSpaceService.captureAttempts().size(), 1);
    QCOMPARE(lowFault.count(), 1);

    QTemporaryDir queueDirectory;
    QSemaphore entered;
    QSemaphore release;
    EvidenceService queueService(
        queueDirectory.path(), {}, ampleSpace,
        [&](const QImage &image, const QString &path) {
            entered.release();
            release.acquire();
            return AtomicPngWriter::write(image, path);
        });
    QVERIFY(queueService.initialize());
    QSignalSpy completed(&queueService, &EvidenceService::captureCompleted);
    for (int index = 0; index < 4; ++index) {
        QVERIFY(queueService.capture(requestFor(
            QStringLiteral("event-%1").arg(index), 0)).accepted);
    }
    if (!entered.tryAcquire(1, 1000)) {
        release.release(4);
        QFAIL("bounded evidence writer did not start");
    }
    const auto overflow = queueService.capture(
        requestFor(QStringLiteral("event-overflow"), 0));
    release.release(4);
    QVERIFY(!overflow.accepted);
    QCOMPARE(overflow.failure, EvidenceCaptureFailure::QueueFull);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 4, 5000);
}

void EvidenceServiceTest::rejectsCatalogPathTraversalOnRecovery()
{
    QTemporaryDir directory;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    EvidenceRecord record;
    record.evidenceId = QStringLiteral("unsafe-evidence");
    record.eventId = QStringLiteral("event-unsafe");
    record.streamId = 9;
    record.captureRequestedAtUtc = now;
    record.capturedAtUtc = now;
    record.writtenAtUtc = now;
    record.frameFreshnessMs = 0;
    record.sourcePlaybackState = QStringLiteral("playing");
    record.relativePath = QStringLiteral("../outside.png");
    record.actor = QStringLiteral("tester");
    record.lastVerifiedAtUtc = now;
    EvidenceCatalogStore store(directory.filePath(QStringLiteral("catalog-v1.json")));
    QString error;
    QVERIFY2(store.save({record}, {}, {}, now, &error), qPrintable(error));
    EvidenceService service(directory.path(), {}, ampleSpace);
    QVERIFY(service.initialize());
    QCOMPARE(service.records().first().availability,
             EvidenceAvailability::UnsafePath);
}

QTEST_GUILESS_MAIN(EvidenceServiceTest)
#include "EvidenceServiceTest.moc"
