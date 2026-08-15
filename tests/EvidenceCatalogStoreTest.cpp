#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "evidence/EvidenceCatalogStore.h"

class EvidenceCatalogStoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void savesLoadsUnicodeWithoutContentHash();
    void preservesCorruptAndHigherSchemas();
};

void EvidenceCatalogStoreTest::savesLoadsUnicodeWithoutContentHash()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("catalog.json"));
    EvidenceCatalogStore store(path);
    QVERIFY(store.load().ok);

    EvidenceRecord record;
    record.evidenceId = QStringLiteral("evidence-01");
    record.eventId = QStringLiteral("事件-01");
    record.deviceId = QStringLiteral("设备-甲");
    record.streamId = 7;
    record.captureRequestedAtUtc = QDateTime::currentDateTimeUtc();
    record.capturedAtUtc = record.captureRequestedAtUtc;
    record.writtenAtUtc = record.captureRequestedAtUtc;
    record.frameFreshnessMs = 10;
    record.sourcePlaybackState = QStringLiteral("playing");
    record.relativePath = QStringLiteral("objects/ev/evidence-01.png");
    record.sizeBytes = 123;
    record.actor = QStringLiteral("本机用户");
    record.lastVerifiedAtUtc = record.captureRequestedAtUtc;
    QString error;
    QVERIFY2(store.save({record}, {}, {}, record.writtenAtUtc, &error),
             qPrintable(error));
    const EvidenceCatalogLoadResult loaded = store.load();
    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(loaded.records.size(), 1);
    QCOMPARE(loaded.records.first().deviceId, QStringLiteral("设备-甲"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray payload = file.readAll();
    QVERIFY(!payload.toLower().contains("sha256"));
    QVERIFY(!payload.contains("contentHash"));
}

void EvidenceCatalogStoreTest::preservesCorruptAndHigherSchemas()
{
    QTemporaryDir directory;
    const QString corruptPath = directory.filePath(QStringLiteral("corrupt.json"));
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("{broken");
    corrupt.close();
    EvidenceCatalogStore corruptStore(corruptPath);
    const auto corruptResult = corruptStore.load();
    QVERIFY(!corruptResult.ok);
    QVERIFY(corruptResult.writeBlocked);
    QFile preserved(corruptPath);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArray("{broken"));

    const QString higherPath = directory.filePath(QStringLiteral("higher.json"));
    QFile higher(higherPath);
    QVERIFY(higher.open(QIODevice::WriteOnly));
    higher.write(QJsonDocument(QJsonObject {
        {QStringLiteral("schemaVersion"), 2},
    }).toJson());
    higher.close();
    const auto higherResult = EvidenceCatalogStore(higherPath).load();
    QVERIFY(!higherResult.ok);
    QVERIFY(higherResult.error.contains(QStringLiteral("高于")));
}

QTEST_APPLESS_MAIN(EvidenceCatalogStoreTest)
#include "EvidenceCatalogStoreTest.moc"
