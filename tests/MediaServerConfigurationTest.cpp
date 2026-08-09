#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "server/MediaServerConfiguration.h"

namespace {

// 写入临时 INI 文件；返回空串表示写入失败，由用例 QVERIFY 暴露。
[[nodiscard]] QString writeIni(
    QTemporaryDir &dir,
    const QString &content
)
{
    const QString path = dir.filePath(QStringLiteral("media-server.ini"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    file.write(content.toUtf8());
    return path;
}

} // namespace

class MediaServerConfigurationTest final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileReturnsDefaultsWithWarning();
    void validServerSectionOverridesDefaults();
    void invalidPortFallsBack_data();
    void invalidPortFallsBack();
    void apiHealthEnabledSwitch_data();
    void apiHealthEnabledSwitch();
    void invalidHostFallsBack_data();
    void invalidHostFallsBack();
    void invalidApiBaseUrlFallsBack();
    void unsafeApiBaseUrlFallsBackWithoutLeakingSecret_data();
    void unsafeApiBaseUrlFallsBackWithoutLeakingSecret();
    void cameraProfilesAreParsed();
    void duplicateCameraIdIsSkipped();
    void duplicateStreamKeyIsSkipped();
    void invalidStreamKeyIsSkipped();
    void seventeenthProfileIsSkipped();
};

void MediaServerConfigurationTest::missingFileReturnsDefaultsWithWarning()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QStringList warnings;
    const MediaServerEndpoint endpoint =
        MediaServerConfiguration::loadEndpoint(
            dir.filePath(QStringLiteral("absent.ini")),
            &warnings
        );
    QCOMPARE(endpoint.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(endpoint.rtmpPort, 1935);
    QCOMPARE(endpoint.application, QStringLiteral("live"));
    QCOMPARE(
        endpoint.apiBaseUrl,
        QUrl(QStringLiteral("http://127.0.0.1:1985"))
    );
    QVERIFY(endpoint.apiHealthEnabled);
    QVERIFY(!warnings.isEmpty());
}

void MediaServerConfigurationTest::validServerSectionOverridesDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral(
            "[server]\n"
            "host=192.168.10.5\n"
            "rtmpPort=1936\n"
            "application=hd\n"
            "apiBaseUrl=http://192.168.10.5:1985\n"
            "apiHealthEnabled=false\n"
        )
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const MediaServerEndpoint endpoint =
        MediaServerConfiguration::loadEndpoint(path, &warnings);
    QCOMPARE(endpoint.host, QStringLiteral("192.168.10.5"));
    QCOMPARE(endpoint.rtmpPort, 1936);
    QCOMPARE(endpoint.application, QStringLiteral("hd"));
    QCOMPARE(
        endpoint.apiBaseUrl,
        QUrl(QStringLiteral("http://192.168.10.5:1985"))
    );
    QVERIFY(!endpoint.apiHealthEnabled);
    QVERIFY(warnings.isEmpty());
}

void MediaServerConfigurationTest::invalidPortFallsBack_data()
{
    QTest::addColumn<QString>("portText");
    QTest::newRow("not a number") << QStringLiteral("abc");
    QTest::newRow("too large") << QStringLiteral("70000");
    QTest::newRow("zero") << QStringLiteral("0");
    QTest::newRow("negative") << QStringLiteral("-1");
}

void MediaServerConfigurationTest::invalidPortFallsBack()
{
    QFETCH(QString, portText);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral("[server]\nrtmpPort=%1\n").arg(portText)
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const MediaServerEndpoint endpoint =
        MediaServerConfiguration::loadEndpoint(path, &warnings);
    QCOMPARE(endpoint.rtmpPort, 1935);
    QVERIFY(!warnings.isEmpty());
}

void MediaServerConfigurationTest::apiHealthEnabledSwitch_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("expected");
    QTest::addColumn<bool>("expectWarning");
    QTest::newRow("false") << QStringLiteral("false") << false << false;
    QTest::newRow("0") << QStringLiteral("0") << false << false;
    QTest::newRow("true") << QStringLiteral("true") << true << false;
    QTest::newRow("1") << QStringLiteral("1") << true << false;
    QTest::newRow("invalid falls back to default")
        << QStringLiteral("maybe") << true << true;
}

void MediaServerConfigurationTest::apiHealthEnabledSwitch()
{
    QFETCH(QString, text);
    QFETCH(bool, expected);
    QFETCH(bool, expectWarning);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral("[server]\napiHealthEnabled=%1\n").arg(text)
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const MediaServerEndpoint endpoint =
        MediaServerConfiguration::loadEndpoint(path, &warnings);
    QCOMPARE(endpoint.apiHealthEnabled, expected);
    QCOMPARE(!warnings.isEmpty(), expectWarning);
}

void MediaServerConfigurationTest::invalidHostFallsBack_data()
{
    QTest::addColumn<QString>("host");
    QTest::newRow("empty") << QString();
    QTest::newRow("path") << QStringLiteral("host/live");
    QTest::newRow("whitespace") << QStringLiteral("bad host");
    QTest::newRow("scheme") << QStringLiteral("rtmp://127.0.0.1");
}

void MediaServerConfigurationTest::invalidHostFallsBack()
{
    QFETCH(QString, host);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral("[server]\nhost=%1\n").arg(host)
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const MediaServerEndpoint endpoint =
        MediaServerConfiguration::loadEndpoint(path, &warnings);
    QCOMPARE(endpoint.host, QStringLiteral("127.0.0.1"));
    QVERIFY(!warnings.isEmpty());
}

void MediaServerConfigurationTest::invalidApiBaseUrlFallsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral("[server]\napiBaseUrl=not a url\n")
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const MediaServerEndpoint endpoint =
        MediaServerConfiguration::loadEndpoint(path, &warnings);
    QCOMPARE(
        endpoint.apiBaseUrl,
        QUrl(QStringLiteral("http://127.0.0.1:1985"))
    );
    QVERIFY(!warnings.isEmpty());
}

void MediaServerConfigurationTest::unsafeApiBaseUrlFallsBackWithoutLeakingSecret_data()
{
    QTest::addColumn<QString>("url");
    QTest::newRow("userinfo")
        << QStringLiteral("http://admin:top-secret@127.0.0.1:1985");
    QTest::newRow("query")
        << QStringLiteral("http://127.0.0.1:1985?token=top-secret");
    QTest::newRow("fragment")
        << QStringLiteral("http://127.0.0.1:1985#top-secret");
}

void MediaServerConfigurationTest::unsafeApiBaseUrlFallsBackWithoutLeakingSecret()
{
    QFETCH(QString, url);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral("[server]\napiBaseUrl=%1\n").arg(url)
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const MediaServerEndpoint endpoint =
        MediaServerConfiguration::loadEndpoint(path, &warnings);
    QCOMPARE(
        endpoint.apiBaseUrl,
        QUrl(QStringLiteral("http://127.0.0.1:1985"))
    );
    QVERIFY(!warnings.isEmpty());
    QVERIFY(!warnings.join(QLatin1Char(' ')).contains(
        QStringLiteral("top-secret")
    ));
}

void MediaServerConfigurationTest::cameraProfilesAreParsed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral(
            "[server]\n"
            "host=127.0.0.1\n"
            "\n"
            "[camera01]\n"
            "cameraId=camera01\n"
            "displayName=正门\n"
            "streamKey=camera01\n"
            "autoStart=false\n"
            "\n"
            "[camera02]\n"
            "cameraId=camera02\n"
            "streamKey=cam02\n"
        )
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const QList<CameraStreamProfile> profiles =
        MediaServerConfiguration::loadCameraProfiles(path, &warnings);
    QCOMPARE(profiles.size(), 2);
    QVERIFY(warnings.isEmpty());

    QCOMPARE(profiles.at(0).cameraId, QStringLiteral("camera01"));
    QCOMPARE(profiles.at(0).displayName, QStringLiteral("正门"));
    QCOMPARE(profiles.at(0).streamKey, QStringLiteral("camera01"));
    QVERIFY(!profiles.at(0).autoStart);

    QCOMPARE(profiles.at(1).cameraId, QStringLiteral("camera02"));
    // displayName 缺省时回退为 cameraId，autoStart 缺省为 true。
    QCOMPARE(profiles.at(1).displayName, QStringLiteral("camera02"));
    QCOMPARE(profiles.at(1).streamKey, QStringLiteral("cam02"));
    QVERIFY(profiles.at(1).autoStart);
}

void MediaServerConfigurationTest::duplicateCameraIdIsSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral(
            "[camera01]\n"
            "cameraId=dup\n"
            "streamKey=key01\n"
            "\n"
            "[camera02]\n"
            "cameraId=dup\n"
            "streamKey=key02\n"
        )
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const QList<CameraStreamProfile> profiles =
        MediaServerConfiguration::loadCameraProfiles(path, &warnings);
    QCOMPARE(profiles.size(), 1);
    QCOMPARE(profiles.at(0).streamKey, QStringLiteral("key01"));
    QVERIFY(!warnings.isEmpty());
}

void MediaServerConfigurationTest::duplicateStreamKeyIsSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral(
            "[camera01]\n"
            "cameraId=camera01\n"
            "streamKey=dup\n"
            "\n"
            "[camera02]\n"
            "cameraId=camera02\n"
            "streamKey=dup\n"
        )
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const QList<CameraStreamProfile> profiles =
        MediaServerConfiguration::loadCameraProfiles(path, &warnings);
    QCOMPARE(profiles.size(), 1);
    QCOMPARE(profiles.at(0).cameraId, QStringLiteral("camera01"));
    QVERIFY(!warnings.isEmpty());
}

void MediaServerConfigurationTest::invalidStreamKeyIsSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeIni(
        dir,
        QStringLiteral(
            "[camera01]\n"
            "cameraId=camera01\n"
            "streamKey=bad/key\n"
        )
    );
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const QList<CameraStreamProfile> profiles =
        MediaServerConfiguration::loadCameraProfiles(path, &warnings);
    QVERIFY(profiles.isEmpty());
    QVERIFY(!warnings.isEmpty());
}

void MediaServerConfigurationTest::seventeenthProfileIsSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString content;
    for (int index = 1; index <= 17; ++index) {
        const QString name = QStringLiteral("camera%1").arg(index, 2, 10, QLatin1Char('0'));
        content += QStringLiteral(
            "[%1]\n"
            "cameraId=%1\n"
            "streamKey=%1\n"
            "\n"
        ).arg(name);
    }
    const QString path = writeIni(dir, content);
    QVERIFY(!path.isEmpty());

    QStringList warnings;
    const QList<CameraStreamProfile> profiles =
        MediaServerConfiguration::loadCameraProfiles(path, &warnings);
    QCOMPARE(profiles.size(), 16);
    QVERIFY(!warnings.isEmpty());
}

QTEST_GUILESS_MAIN(MediaServerConfigurationTest)

#include "MediaServerConfigurationTest.moc"
