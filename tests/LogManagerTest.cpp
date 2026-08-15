#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "logging/LogConfiguration.h"
#include "logging/LogManager.h"
#include "logging/SensitiveDataSanitizer.h"

namespace {

QByteArray readMatchingFiles(
    const QString &directory,
    const QString &pattern
)
{
    QByteArray result;
    const QDir dir(directory);
    for (const QString &name : dir.entryList({pattern}, QDir::Files)) {
        QFile file(dir.filePath(name));
        if (file.open(QIODevice::ReadOnly)) {
            result += file.readAll();
        }
    }
    return result;
}

} // namespace

class LogManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void parsesLevelsAndSanitizesSensitiveData();
    void separatesSystemAndAuditAndKeepsAuditUnfiltered();
    void rotatesAndBoundsFiles();
    void reportsQueueOverflowAndFlushesOnShutdown();
    void loadsIniConfiguration();
    void invalidDirectoryDoesNotCrash();
};

void LogManagerTest::parsesLevelsAndSanitizesSensitiveData()
{
    LogLevel level = LogLevel::Info;
    QVERIFY(LogManager::parseLevel(QStringLiteral("trace"), &level));
    QCOMPARE(level, LogLevel::Trace);
    QVERIFY(LogManager::parseLevel(QStringLiteral("critical"), &level));
    QCOMPARE(level, LogLevel::Critical);

    const QString sanitized = SensitiveDataSanitizer::sanitizeText(
        QStringLiteral(
            "url=rtmp://user:password@example.com:1935/live/private-key"
            "?token=secret password=hunter2 token=abc"
        )
    );
    QVERIFY(sanitized.contains(QStringLiteral("example.com:1935")));
    QVERIFY(sanitized.contains(QStringLiteral("/live/***")));
    QVERIFY(!sanitized.contains(QStringLiteral("hunter2")));
    QVERIFY(!sanitized.contains(QStringLiteral("private-key")));
    QVERIFY(!sanitized.contains(QStringLiteral("token=abc")));

    const QJsonObject object = SensitiveDataSanitizer::sanitizeObject({
        {QStringLiteral("password"), QStringLiteral("secret")},
        {QStringLiteral("nested"), QJsonObject {
            {QStringLiteral("privateKey"), QStringLiteral("key")},
            {QStringLiteral("name"), QStringLiteral("Camera")}
        }}
    });
    QCOMPARE(
        object.value(QStringLiteral("password")).toString(),
        QStringLiteral("***")
    );
    QCOMPARE(
        object.value(QStringLiteral("nested"))
            .toObject()
            .value(QStringLiteral("privateKey"))
            .toString(),
        QStringLiteral("***")
    );
}

void LogManagerTest::separatesSystemAndAuditAndKeepsAuditUnfiltered()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LoggingOptions options;
    options.directoryPath = directory.path();
    options.minimumLevel = LogLevel::Critical;
    options.consoleEnabled = false;

    LogManager manager;
    QVERIFY(manager.initialize(options));
    manager.logSystem(
        LogLevel::Info,
        QStringLiteral("test"),
        QStringLiteral("filtered"),
        QStringLiteral("must not be stored")
    );
    manager.logSystem(
        LogLevel::Critical,
        QStringLiteral("media"),
        QStringLiteral("failure"),
        QStringLiteral(
            "password=secret "
            "rtmp://user:pass@example.com/live/private?token=x"
        ),
        {
            {QStringLiteral("token"), QStringLiteral("raw-token")},
            {QStringLiteral("errorCode"), -110}
        },
        {
            7,
            QStringLiteral("Camera 07"),
            QStringLiteral(
                "rtmp://user:pass@example.com/live/private?token=x"
            )
        }
    );

    AuditRecord audit;
    audit.actor = QStringLiteral("admin");
    audit.action = AuditAction::Login;
    audit.targetType = QStringLiteral("User");
    audit.targetId = QStringLiteral("admin");
    audit.result = AuditResult::Failure;
    audit.reason = QStringLiteral("password=secret");
    audit.beforeValues = {
        {QStringLiteral("token"), QStringLiteral("raw-token")}
    };
    audit.source = QStringLiteral("local-ui");
    manager.logAudit(audit);

    const QString systemPath = manager.systemLogFilePath();
    const QString auditPath = manager.auditLogFilePath();
    manager.shutdown();

    QVERIFY(QFile::exists(systemPath));
    QVERIFY(QFile::exists(auditPath));
    QFile systemFile(systemPath);
    QVERIFY(systemFile.open(QIODevice::ReadOnly));
    const QByteArray systemPayload = systemFile.readAll();
    QVERIFY(systemPayload.contains("\"channel\":\"system\""));
    QVERIFY(systemPayload.contains("\"level\":\"critical\""));
    QVERIFY(!systemPayload.contains("must not be stored"));
    QVERIFY(!systemPayload.contains("raw-token"));
    QVERIFY(!systemPayload.contains("private?"));
    QVERIFY(!systemPayload.contains("password=secret"));

    QFile auditFile(auditPath);
    QVERIFY(auditFile.open(QIODevice::ReadOnly));
    const QByteArray auditPayload = auditFile.readAll();
    QVERIFY(auditPayload.contains("\"channel\":\"audit\""));
    QVERIFY(auditPayload.contains("\"action\":\"LOGIN\""));
    QVERIFY(auditPayload.contains("\"result\":\"FAILURE\""));
    QVERIFY(!auditPayload.contains("raw-token"));
    QVERIFY(!auditPayload.contains("password=secret"));
    QCOMPARE(
        LogManager::auditResultName(AuditResult::Submitted),
        QStringLiteral("SUBMITTED")
    );
    QCOMPARE(
        LogManager::auditResultName(AuditResult::Rejected),
        QStringLiteral("REJECTED")
    );
    QCOMPARE(
        LogManager::auditResultName(AuditResult::PublishFailed),
        QStringLiteral("PUBLISH_FAILED")
    );
}

void LogManagerTest::rotatesAndBoundsFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString expiredPath =
        directory.filePath(QStringLiteral("system.jsonl.99"));
    QFile expiredFile(expiredPath);
    QVERIFY(expiredFile.open(QIODevice::ReadWrite));
    expiredFile.write("expired");
    QVERIFY(expiredFile.setFileTime(
        QDateTime::currentDateTimeUtc().addDays(-30),
        QFileDevice::FileModificationTime
    ));
    expiredFile.close();

    LoggingOptions options;
    options.directoryPath = directory.path();
    options.consoleEnabled = false;
    options.systemFile.maximumFileBytes = 1'024;
    options.systemFile.retainedFileCount = 2;
    options.systemFile.maximumTotalBytes = 4'096;
    options.systemFile.queueCapacity = 128;

    LogManager manager;
    QVERIFY(manager.initialize(options));
    QVERIFY(!QFile::exists(expiredPath));
    for (int index = 0; index < 80; ++index) {
        manager.logSystem(
            LogLevel::Info,
            QStringLiteral("test"),
            QStringLiteral("rotation"),
            QStringLiteral(
                "rotation record %1 with a sufficiently long payload"
            ).arg(index)
        );
    }
    const QString activePath = manager.systemLogFilePath();
    manager.shutdown();

    QVERIFY(QFile::exists(activePath));
    QVERIFY(QFile::exists(activePath + QStringLiteral(".1")));
    const QStringList files = QDir(directory.path()).entryList(
        {QStringLiteral("system.jsonl*")},
        QDir::Files
    );
    QVERIFY(files.size() <= 3);
    qint64 totalBytes = 0;
    for (const QString &name : files) {
        QFile file(directory.filePath(name));
        totalBytes += file.size();
        QVERIFY(file.open(QIODevice::ReadOnly));
        for (const QByteArray &line : file.readAll().split('\n')) {
            if (!line.trimmed().isEmpty()) {
                QVERIFY(QJsonDocument::fromJson(line).isObject());
            }
        }
    }
    QVERIFY(totalBytes <= options.systemFile.maximumTotalBytes);
}

void LogManagerTest::reportsQueueOverflowAndFlushesOnShutdown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LoggingOptions options;
    options.directoryPath = directory.path();
    options.consoleEnabled = false;
    options.systemFile.queueCapacity = 16;
    options.systemFile.maximumFileBytes = 10 * 1024 * 1024;
    options.shutdownFlushTimeoutMs = 5'000;

    LogManager manager;
    QVERIFY(manager.initialize(options));
    for (int index = 0; index < 10'000; ++index) {
        manager.logSystem(
            LogLevel::Info,
            QStringLiteral("test"),
            QStringLiteral("overflow"),
            QStringLiteral("queued record %1").arg(index)
        );
    }
    manager.logSystem(
        LogLevel::Critical,
        QStringLiteral("test"),
        QStringLiteral("critical_tail"),
        QStringLiteral("critical record")
    );
    manager.shutdown();

    const QByteArray payload = readMatchingFiles(
        directory.path(),
        QStringLiteral("system.jsonl*")
    );
    QVERIFY(payload.contains("\"event\":\"queue_overflow\""));
    QVERIFY(payload.contains("\"event\":\"critical_tail\""));
}

void LogManagerTest::loadsIniConfiguration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        directory.filePath(QStringLiteral("rtmp-monitor.ini"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(
        "[logging]\n"
        "directoryPath=C:/example/logs\n"
        "shutdownFlushTimeoutMs=1234\n"
        "[system]\n"
        "level=warning\n"
        "maximumFileBytes=2048\n"
        "[system.modules]\n"
        "media=error\n"
        "[audit]\n"
        "retainedFileCount=9\n"
        "[userMessages]\n"
        "repeatWindowMs=777\n"
    );
    file.close();

    QString error;
    const LoggingOptions options = LogConfiguration::load(path, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(options.minimumLevel, LogLevel::Warning);
    QCOMPARE(options.systemFile.maximumFileBytes, qint64 {2'048});
    QCOMPARE(options.auditFile.retainedFileCount, 9);
    QCOMPARE(options.shutdownFlushTimeoutMs, 1'234);
    QCOMPARE(options.userMessageRepeatWindowMs, 777);
    QCOMPARE(
        options.moduleMinimumLevels.value(QStringLiteral("media")),
        LogLevel::Error
    );
}

void LogManagerTest::invalidDirectoryDoesNotCrash()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("plain-file"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a directory");
    file.close();

    LoggingOptions options;
    options.directoryPath =
        QDir(filePath).filePath(QStringLiteral("logs"));
    options.consoleEnabled = false;

    LogManager manager;
    QVERIFY(!manager.initialize(options));
    manager.logSystem(
        LogLevel::Critical,
        QStringLiteral("test"),
        QStringLiteral("write_failure"),
        QStringLiteral("must not crash")
    );
    manager.logAudit({});
    manager.shutdown();
}

QTEST_GUILESS_MAIN(LogManagerTest)

#include "LogManagerTest.moc"
