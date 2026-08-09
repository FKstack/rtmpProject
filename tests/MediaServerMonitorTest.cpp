#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "server/MediaServerMonitor.h"

namespace {

// 假 HTTP 服务：对任何完整请求返回固定状态和正文，
// 用于替代真实 SRS 的 /api/v1/versions。
class FakeHttpServer final : public QObject
{
public:
    FakeHttpServer()
    {
        connect(
            &server_, &QTcpServer::newConnection,
            this,
            [this]() {
                while (QTcpSocket *socket = server_.nextPendingConnection()) {
                    handleSocket(socket);
                }
            }
        );
    }

    [[nodiscard]] bool listen(quint16 port)
    {
        return server_.listen(QHostAddress::LocalHost, port);
    }

    void close()
    {
        server_.close();
    }

    [[nodiscard]] quint16 serverPort() const
    {
        return server_.serverPort();
    }

    void setResponseBody(const QByteArray &body)
    {
        body_ = body;
    }

private:
    void handleSocket(QTcpSocket *socket)
    {
        socket->setParent(this);
        connect(
            socket, &QTcpSocket::disconnected,
            socket, &QObject::deleteLater
        );
        connect(
            socket, &QTcpSocket::readyRead,
            this,
            [this, socket]() {
                // 请求头可能分片到达；凑齐空行后再响应。
                QByteArray buffer =
                    socket->property("requestBuffer").toByteArray();
                buffer += socket->readAll();
                if (!buffer.contains("\r\n\r\n")) {
                    socket->setProperty("requestBuffer", buffer);
                    return;
                }
                const QByteArray response =
                    QByteArrayLiteral("HTTP/1.1 200 OK\r\n")
                    + QByteArrayLiteral("Content-Type: application/json\r\n")
                    + QByteArrayLiteral("Content-Length: ")
                    + QByteArray::number(body_.size())
                    + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                    + body_;
                socket->write(response);
                socket->flush();
                // 由客户端按 Connection: close 主动断开，服务器侧保持被动
                // 关闭，避免服务器端口进入 TIME_WAIT 影响 flap 场景重绑定。
            }
        );
    }

    QTcpServer server_;
    QByteArray body_ =
        QByteArrayLiteral(R"({"code":0,"data":{"version":"6.0.184"}})");
};

// 仅监听并立即释放连接的假 RTMP 端口：TCP 可达但不提供任何协议。
void attachDiscardHandler(QTcpServer &server, QObject *context)
{
    QObject::connect(
        &server, &QTcpServer::newConnection,
        context,
        [&server]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                socket->deleteLater();
            }
        }
    );
}

// 取一个当前空闲的端口并立即释放，用于构造“无监听”场景。
[[nodiscard]] quint16 reserveFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

[[nodiscard]] QList<MediaServerState> statesFrom(const QSignalSpy &spy)
{
    QList<MediaServerState> states;
    for (const QList<QVariant> &arguments : spy) {
        states.append(
            arguments.constFirst().value<MediaServerHealth>().state
        );
    }
    return states;
}

[[nodiscard]] MediaServerState lastStateOf(const QSignalSpy &spy)
{
    if (spy.isEmpty()) {
        return MediaServerState::Unknown;
    }
    return spy.constLast().constFirst().value<MediaServerHealth>().state;
}

[[nodiscard]] MediaServerHealth lastHealthOf(const QSignalSpy &spy)
{
    return spy.constLast().constFirst().value<MediaServerHealth>();
}

} // namespace

class MediaServerMonitorTest final : public QObject
{
    Q_OBJECT

private slots:
    void tcpOnlyServerReportsDegraded();
    void healthyApiReportsHealthyWithVersion();
    void invalidApiResponsesReportDegraded_data();
    void invalidApiResponsesReportDegraded();
    void apiTimeoutReportsDegraded();
    void endpointChangeClearsPreviousVersion();
    void noListenerReportsUnavailable();
    void flappingDoesNotThrashState();
};

void MediaServerMonitorTest::tcpOnlyServerReportsDegraded()
{
    QTcpServer rtmpServer;
    QVERIFY(rtmpServer.listen(QHostAddress::LocalHost, 0));
    attachDiscardHandler(rtmpServer, this);
    const quint16 apiPort = reserveFreePort();
    QVERIFY(apiPort != 0);

    MediaServerMonitor monitor;
    monitor.setTimingForTesting(60, 40, 3, 2);
    MediaServerEndpoint endpoint;
    endpoint.rtmpPort = rtmpServer.serverPort();
    endpoint.apiBaseUrl =
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(apiPort));
    monitor.setEndpoint(endpoint);

    QSignalSpy spy(&monitor, &MediaServerMonitor::healthChanged);
    monitor.startMonitoring();

    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Degraded,
        5'000
    );
    QVERIFY(!statesFrom(spy).contains(MediaServerState::Unavailable));
    const MediaServerHealth health = lastHealthOf(spy);
    QVERIFY(health.rtmpPortReachable);
    QVERIFY(!health.apiReachable);
    monitor.stopMonitoring();
}

void MediaServerMonitorTest::healthyApiReportsHealthyWithVersion()
{
    QTcpServer rtmpServer;
    QVERIFY(rtmpServer.listen(QHostAddress::LocalHost, 0));
    attachDiscardHandler(rtmpServer, this);
    FakeHttpServer apiServer;
    QVERIFY(apiServer.listen(0));

    MediaServerMonitor monitor;
    monitor.setTimingForTesting(60, 40, 3, 2);
    MediaServerEndpoint endpoint;
    endpoint.rtmpPort = rtmpServer.serverPort();
    endpoint.apiBaseUrl = QUrl(
        QStringLiteral("http://127.0.0.1:%1").arg(apiServer.serverPort())
    );
    monitor.setEndpoint(endpoint);

    QSignalSpy spy(&monitor, &MediaServerMonitor::healthChanged);
    monitor.startMonitoring();

    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Healthy,
        5'000
    );
    const MediaServerHealth health = lastHealthOf(spy);
    QVERIFY(health.rtmpPortReachable);
    QVERIFY(health.apiReachable);
    QCOMPARE(health.serverVersion, QStringLiteral("6.0.184"));
    // 只有 Checking 与 Healthy 两次发射；稳定后不再逐次刷屏。
    QCOMPARE(spy.count(), 2);
    QTest::qWait(300);
    QCOMPARE(spy.count(), 2);
    monitor.stopMonitoring();
}

void MediaServerMonitorTest::invalidApiResponsesReportDegraded_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::newRow("non json") << QByteArrayLiteral("this is not json");
    QTest::newRow("non zero code")
        << QByteArrayLiteral(R"({"code":500,"data":{}})");
    QTest::newRow("missing version")
        << QByteArrayLiteral(R"({"code":0,"data":{}})");
    QTest::newRow("blank version")
        << QByteArrayLiteral(R"({"code":0,"data":{"version":"  "}})");
}

void MediaServerMonitorTest::invalidApiResponsesReportDegraded()
{
    QFETCH(QByteArray, body);
    QTcpServer rtmpServer;
    QVERIFY(rtmpServer.listen(QHostAddress::LocalHost, 0));
    attachDiscardHandler(rtmpServer, this);
    FakeHttpServer apiServer;
    apiServer.setResponseBody(body);
    QVERIFY(apiServer.listen(0));

    MediaServerMonitor monitor;
    monitor.setTimingForTesting(60, 40, 3, 2);
    MediaServerEndpoint endpoint;
    endpoint.rtmpPort = rtmpServer.serverPort();
    endpoint.apiBaseUrl = QUrl(
        QStringLiteral("http://127.0.0.1:%1").arg(apiServer.serverPort())
    );
    monitor.setEndpoint(endpoint);

    QSignalSpy spy(&monitor, &MediaServerMonitor::healthChanged);
    monitor.startMonitoring();

    // RTMP 可达而 API 异常时必须停在 Degraded，不得升级为 Unavailable。
    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Degraded,
        5'000
    );
    QTest::qWait(300);
    QVERIFY(!statesFrom(spy).contains(MediaServerState::Unavailable));
    monitor.stopMonitoring();
}

void MediaServerMonitorTest::apiTimeoutReportsDegraded()
{
    QTcpServer rtmpServer;
    QVERIFY(rtmpServer.listen(QHostAddress::LocalHost, 0));
    attachDiscardHandler(rtmpServer, this);

    QTcpServer silentApiServer;
    QVERIFY(silentApiServer.listen(QHostAddress::LocalHost, 0));
    connect(
        &silentApiServer, &QTcpServer::newConnection,
        this,
        [&silentApiServer]() {
            while (QTcpSocket *socket = silentApiServer.nextPendingConnection()) {
                socket->setParent(&silentApiServer);
            }
        }
    );

    MediaServerMonitor monitor;
    monitor.setTimingForTesting(80, 30, 1, 1);
    MediaServerEndpoint endpoint;
    endpoint.rtmpPort = rtmpServer.serverPort();
    endpoint.apiBaseUrl = QUrl(
        QStringLiteral("http://127.0.0.1:%1")
            .arg(silentApiServer.serverPort())
    );
    monitor.setEndpoint(endpoint);

    QSignalSpy spy(&monitor, &MediaServerMonitor::healthChanged);
    monitor.startMonitoring();
    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Degraded,
        5'000
    );
    QVERIFY(lastHealthOf(spy).diagnostic.contains(
        QStringLiteral("timed out"), Qt::CaseInsensitive
    ));
    monitor.stopMonitoring();
}

void MediaServerMonitorTest::endpointChangeClearsPreviousVersion()
{
    QTcpServer rtmpServer;
    QVERIFY(rtmpServer.listen(QHostAddress::LocalHost, 0));
    attachDiscardHandler(rtmpServer, this);
    FakeHttpServer apiServer;
    QVERIFY(apiServer.listen(0));

    MediaServerMonitor monitor;
    monitor.setTimingForTesting(60, 40, 1, 1);
    MediaServerEndpoint endpoint;
    endpoint.rtmpPort = rtmpServer.serverPort();
    endpoint.apiBaseUrl = QUrl(
        QStringLiteral("http://127.0.0.1:%1").arg(apiServer.serverPort())
    );
    monitor.setEndpoint(endpoint);

    QSignalSpy spy(&monitor, &MediaServerMonitor::healthChanged);
    monitor.startMonitoring();
    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Healthy,
        5'000
    );
    QCOMPARE(lastHealthOf(spy).serverVersion, QStringLiteral("6.0.184"));

    apiServer.setResponseBody(QByteArrayLiteral(R"({"code":0,"data":{}})"));
    monitor.setEndpoint(endpoint);
    monitor.probeNow();
    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Degraded,
        5'000
    );
    QVERIFY(lastHealthOf(spy).serverVersion.isEmpty());
    monitor.stopMonitoring();
}

void MediaServerMonitorTest::noListenerReportsUnavailable()
{
    const quint16 rtmpPort = reserveFreePort();
    const quint16 apiPort = reserveFreePort();
    QVERIFY(rtmpPort != 0);
    QVERIFY(apiPort != 0);

    MediaServerMonitor monitor;
    monitor.setTimingForTesting(60, 40, 3, 2);
    MediaServerEndpoint endpoint;
    endpoint.rtmpPort = rtmpPort;
    endpoint.apiBaseUrl =
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(apiPort));
    monitor.setEndpoint(endpoint);

    QSignalSpy spy(&monitor, &MediaServerMonitor::healthChanged);
    monitor.startMonitoring();

    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Unavailable,
        5'000
    );
    const MediaServerHealth health = lastHealthOf(spy);
    QVERIFY(!health.rtmpPortReachable);
    QVERIFY(!health.apiReachable);
    QVERIFY(!health.diagnostic.isEmpty());
    monitor.stopMonitoring();
}

void MediaServerMonitorTest::flappingDoesNotThrashState()
{
    QTcpServer rtmpServer;
    QVERIFY(rtmpServer.listen(QHostAddress::LocalHost, 0));
    attachDiscardHandler(rtmpServer, this);
    FakeHttpServer apiServer;
    QVERIFY(apiServer.listen(0));
    const quint16 rtmpPort = rtmpServer.serverPort();
    const quint16 apiPort = apiServer.serverPort();

    MediaServerMonitor monitor;
    // 失败阈值 4、间隔 100ms：约 250ms 的短暂掉线最多积累 3 次失败，
    // 不得触发状态切换。探测超时取 80ms，为 qWait 事件分片处理
    // 下的 HTTP 往返留出余量，避免超时抖动干扰防抖断言。
    monitor.setTimingForTesting(100, 80, 4, 2);
    MediaServerEndpoint endpoint;
    endpoint.rtmpPort = rtmpPort;
    endpoint.apiBaseUrl =
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(apiPort));
    monitor.setEndpoint(endpoint);

    QSignalSpy spy(&monitor, &MediaServerMonitor::healthChanged);
    QStringList emissionTrace;
    connect(
        &monitor, &MediaServerMonitor::healthChanged,
        this,
        [&emissionTrace](const MediaServerHealth &health) {
            emissionTrace.append(
                QStringLiteral("state=%1 rtmp=%2 api=%3 diag=%4")
                    .arg(static_cast<int>(health.state))
                    .arg(health.rtmpPortReachable)
                    .arg(health.apiReachable)
                    .arg(health.diagnostic)
            );
        }
    );
    monitor.startMonitoring();
    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Healthy,
        5'000
    );
    QCOMPARE(spy.count(), 2);

    for (int flap = 0; flap < 2; ++flap) {
        rtmpServer.close();
        apiServer.close();
        QTest::qWait(250);
        QVERIFY(rtmpServer.listen(QHostAddress::LocalHost, rtmpPort));
        QVERIFY(apiServer.listen(apiPort));
        QTest::qWait(500);
        QVERIFY2(
            lastStateOf(spy) == MediaServerState::Healthy,
            qPrintable(emissionTrace.join(QStringLiteral(" | ")))
        );
        QCOMPARE(spy.count(), 2);
    }

    // 持续下线超过防抖阈值后必须报告 Unavailable。
    rtmpServer.close();
    apiServer.close();
    QTRY_VERIFY_WITH_TIMEOUT(
        lastStateOf(spy) == MediaServerState::Unavailable,
        5'000
    );
    QCOMPARE(spy.count(), 3);
    monitor.stopMonitoring();
}

QTEST_GUILESS_MAIN(MediaServerMonitorTest)

#include "MediaServerMonitorTest.moc"
