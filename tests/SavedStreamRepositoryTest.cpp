#include <QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>

#include "profiles/SavedStreamRepository.h"

class SavedStreamRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileUsesEmptyList();
    void roundTripsUnicodeAndStableIds();
    void rejectsDuplicateAndInvalidEntries();
    void preservesCorruptFile();
    void rejectsFutureSchema();
};

void SavedStreamRepositoryTest::missingFileUsesEmptyList()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SavedStreamRepository repository(directory.filePath("streams.json"));
    const SavedStreamLoadResult result = repository.load();
    QVERIFY(result.ok());
    QVERIFY(!result.fileExists);
    QVERIFY(result.profiles.isEmpty());
}

void SavedStreamRepositoryTest::roundTripsUnicodeAndStableIds()
{
    QTemporaryDir directory;
    SavedStreamRepository repository(directory.filePath("streams.json"));
    const QList<SavedStreamProfile> expected{{
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        QStringLiteral("前门摄像头"),
        QStringLiteral("rtmp://example.test/live/camera001"),
        true
    }};
    QString error;
    QVERIFY2(repository.save(expected, &error), qPrintable(error));
    const SavedStreamLoadResult loaded = repository.load();
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.profiles, expected);
}

void SavedStreamRepositoryTest::rejectsDuplicateAndInvalidEntries()
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString error;
    QVERIFY(!SavedStreamRepository::validate({
        {id, QStringLiteral("A"), QStringLiteral("http://host/live/a"), true}
    }, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!SavedStreamRepository::validate({
        {id, QStringLiteral("A"), QStringLiteral("rtmp://host/live/a"), true},
        {QUuid::createUuid().toString(QUuid::WithoutBraces),
         QStringLiteral("B"), QStringLiteral("RTMP://HOST/live/a"), false}
    }, &error));
}

void SavedStreamRepositoryTest::preservesCorruptFile()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("streams.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray original("{not-json");
    QCOMPARE(file.write(original), original.size());
    file.close();

    SavedStreamRepository repository(path);
    const SavedStreamLoadResult result = repository.load();
    QVERIFY(!result.ok());
    QFile unchanged(path);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original);
}

void SavedStreamRepositoryTest::rejectsFutureSchema()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("streams.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("profiles"), QJsonArray{}}
    }).toJson());
    file.close();
    const SavedStreamLoadResult result = SavedStreamRepository(path).load();
    QVERIFY(!result.ok());
    QVERIFY(result.profiles.isEmpty());
}

QTEST_MAIN(SavedStreamRepositoryTest)
#include "SavedStreamRepositoryTest.moc"
