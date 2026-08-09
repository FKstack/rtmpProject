#include <QTest>

#include <optional>

#include "server/RtmpUrlBuilder.h"

class RtmpUrlBuilderTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultEndpointBuildsLocalUrl();
    void ipv4EndpointBuildsExpectedUrl();
    void ipv6HostGetsBrackets();
    void rejectsInvalidInput_data();
    void rejectsInvalidInput();
    void successClearsErrorAndNullErrorPointerIsSafe();
};

void RtmpUrlBuilderTest::defaultEndpointBuildsLocalUrl()
{
    const std::optional<QUrl> url =
        buildRtmpUrl(MediaServerEndpoint{}, QStringLiteral("camera01"));
    QVERIFY(url.has_value());
    QCOMPARE(
        url->toString(),
        QStringLiteral("rtmp://127.0.0.1:1935/live/camera01")
    );
}

void RtmpUrlBuilderTest::ipv4EndpointBuildsExpectedUrl()
{
    MediaServerEndpoint endpoint;
    endpoint.host = QStringLiteral("192.168.1.10");
    endpoint.rtmpPort = 1936;
    endpoint.application = QStringLiteral("live2");
    const std::optional<QUrl> url =
        buildRtmpUrl(endpoint, QStringLiteral("cam-01_A"));
    QVERIFY(url.has_value());
    QCOMPARE(url->scheme(), QStringLiteral("rtmp"));
    QCOMPARE(url->host(), QStringLiteral("192.168.1.10"));
    QCOMPARE(url->port(), 1936);
    QCOMPARE(url->path(), QStringLiteral("/live2/cam-01_A"));
    QCOMPARE(
        url->toString(),
        QStringLiteral("rtmp://192.168.1.10:1936/live2/cam-01_A")
    );
}

void RtmpUrlBuilderTest::ipv6HostGetsBrackets()
{
    MediaServerEndpoint endpoint;
    endpoint.host = QStringLiteral("::1");
    const std::optional<QUrl> url =
        buildRtmpUrl(endpoint, QStringLiteral("camera01"));
    QVERIFY(url.has_value());
    QCOMPARE(url->host(), QStringLiteral("::1"));
    QCOMPARE(
        url->toString(),
        QStringLiteral("rtmp://[::1]:1935/live/camera01")
    );
}

void RtmpUrlBuilderTest::rejectsInvalidInput_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<int>("port");
    QTest::addColumn<QString>("application");
    QTest::addColumn<QString>("streamKey");

    const QString host = QStringLiteral("127.0.0.1");
    const QString app = QStringLiteral("live");
    const QString key = QStringLiteral("camera01");

    QTest::newRow("empty host") << QString() << 1935 << app << key;
    QTest::newRow("host with space")
        << QStringLiteral("192.168.0. 1") << 1935 << app << key;
    QTest::newRow("host with path")
        << QStringLiteral("example.com/live") << 1935 << app << key;
    QTest::newRow("zero port") << host << 0 << app << key;
    QTest::newRow("application with slash")
        << host << 1935 << QStringLiteral("live/room") << key;
    QTest::newRow("application with space")
        << host << 1935 << QStringLiteral("my app") << key;
    QTest::newRow("application with chinese")
        << host << 1935 << QStringLiteral("应用") << key;
    QTest::newRow("empty stream key") << host << 1935 << app << QString();
    QTest::newRow("stream key with slash")
        << host << 1935 << app << QStringLiteral("cam/01");
    QTest::newRow("stream key with space")
        << host << 1935 << app << QStringLiteral("cam 01");
    QTest::newRow("stream key with chinese")
        << host << 1935 << app << QStringLiteral("摄像头");
    QTest::newRow("stream key with query")
        << host << 1935 << app << QStringLiteral("cam01?token=x");
}

void RtmpUrlBuilderTest::rejectsInvalidInput()
{
    QFETCH(QString, host);
    QFETCH(int, port);
    QFETCH(QString, application);
    QFETCH(QString, streamKey);

    MediaServerEndpoint endpoint;
    endpoint.host = host;
    endpoint.rtmpPort = static_cast<quint16>(port);
    endpoint.application = application;

    QString error;
    const std::optional<QUrl> url =
        buildRtmpUrl(endpoint, streamKey, &error);
    QVERIFY(!url.has_value());
    QVERIFY(!error.isEmpty());
}

void RtmpUrlBuilderTest::successClearsErrorAndNullErrorPointerIsSafe()
{
    QString error = QStringLiteral("stale");
    const std::optional<QUrl> url = buildRtmpUrl(
        MediaServerEndpoint{},
        QStringLiteral("camera01"),
        &error
    );
    QVERIFY(url.has_value());
    QVERIFY(error.isEmpty());

    const std::optional<QUrl> rejected = buildRtmpUrl(
        MediaServerEndpoint{},
        QStringLiteral("bad/key")
    );
    QVERIFY(!rejected.has_value());
}

QTEST_GUILESS_MAIN(RtmpUrlBuilderTest)

#include "RtmpUrlBuilderTest.moc"
