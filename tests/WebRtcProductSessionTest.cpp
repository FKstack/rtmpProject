#include "logging/LogManager.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/MainWindow.h"
#include "ui/VideoCanvasHost.h"
#include "webrtc_dev/SessionPackage.h"
#include "webrtc_product/WebRtcProductSessionController.h"
#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QAction>
#include <QByteArray>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

#include <chrono>
#include <optional>

using namespace rtmp_monitor::webrtc_dev;
using namespace rtmp_monitor::webrtc_product;
using namespace rtmp_monitor::webrtc_transport;

namespace {

H264AccessUnit fixedRedIdr(std::int64_t timestampUs)
{
    const QByteArray bytes = QByteArray::fromBase64(
        "AAAAAWdCwB/cQmwEQAAAAwBAAAADAIPGDOAAAAABaM4PLIAAAAEGBf//WdxF6b3m2Ui3lizYINkj7u94MjY0IC0gY29yZSAxNjUgcjMyMjMgMDQ4MGNiMCAtIEguMjY0L01QRUctNCBBVkMgY29kZWMgLSBDb3B5bGVmdCAyMDAzLTIwMjUgLSBodHRwOi8vd3d3LnZpZGVvbGFuLm9yZy94MjY0Lmh0bWwgLSBvcHRpb25zOiBjYWJhYz0wIHJlZj0xIGRlYmxvY2s9MTowOjAgYW5hbHlzZT0weDE6MHgxMTEgbWU9aGV4IHN1Ym1lPTcgcHN5PTEgcHN5X3JkPTEuMDA6MC4wMiBtaXhlZF9yZWY9MCBtZV9yYW5nZT0xNiBjaHJvbWFfbWU9MSB0cmVsbGlzPTEgOHg4ZGN0PTAgY3FtPTAgZGVhZHpvbmU9MjEsMTEgZmFzdF9wc2tpcD0xIGNocm9tYV9xcF9vZmZzZXQ9LTIgdGhyZWFkcz0yIGxvb2thaGVhZF90aHJlYWRzPTEgc2xpY2VkX3RocmVhZHM9MCBucj0wIGRlY2ltYXRlPTEgaW50ZXJsYWNlZD0wIGJsdXJheV9jb21wYXQ9MCBjb25zdHJhaW5lZF9pbnRyYT0wIGJmcmFtZXM9MCB3ZWlnaHRwPTAga2V5aW50PTEga2V5aW50X21pbj0xIHNjZW5lY3V0PTAgaW50cmFfcmVmcmVzaD0wIHJjPWNyZiBtYnRyZWU9MCBjcmY9MjMuMCBxY29tcD0wLjYwIHFwbWluPTAgcXBtYXg9NjkgcXBzdGVwPTQgaXBfcmF0aW89MS40MCBhcT0xOjEuMDAAgAAAAWWIhAS8RigACovHAAEo2OAAL60nJyddddddddddddeA"
    );
    H264AccessUnit result;
    result.annexB.assign(bytes.cbegin(), bytes.cend());
    result.mediaTimestampUs = timestampUs;
    result.keyFrame = true;
    return result;
}

struct LocatedPackage
{
    QString path;
    SessionPackage package;
};

std::optional<LocatedPackage> waitForPackage(
    SessionPackageStore &store,
    SessionRole role,
    const QString &sessionId = {}
)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 20'000) {
        for (const QString &path : store.managedFiles(role)) {
            const SessionFileResult read = store.read(path);
            if (!read.ok()) continue;
            const SessionResult decoded =
                SessionPackageCodec::decodeAndValidate(
                    read.bytes,
                    QDateTime::currentDateTimeUtc(),
                    SessionExpectation {role, sessionId, false}
                );
            if (decoded.ok()) return LocatedPackage {path, *decoded.package};
        }
        QTest::qWait(25);
    }
    return std::nullopt;
}

} // namespace

class WebRtcProductSessionTest final : public QObject
{
    Q_OBJECT

private slots:
    void requestAndStatePolicyAreBounded();
    void productReceivePath_data();
    void productReceivePath();
    void cleanupTestCase();
};

void WebRtcProductSessionTest::requestAndStatePolicyAreBounded()
{
    WebRtcSessionRequest request;
    QString error;
    QVERIFY(WebRtcProductPolicy::validateRequest(request, &error));
    QVERIFY(error.isEmpty());

    request.displayName.clear();
    QVERIFY(!WebRtcProductPolicy::validateRequest(request, &error));
    request.displayName = QStringLiteral("temporary");
    IceServerRuntimeConfig turn;
    turn.urls.push_back("turn:relay.invalid:3478");
    request.ice.servers.push_back(turn);
    QVERIFY(!WebRtcProductPolicy::validateRequest(request, &error));

    QCOMPARE(
        WebRtcProductPolicy::classifyConnectionFailure(
            EndpointError::ConnectionFailed,
            EndpointIceState::Failed,
            {"host", "srflx"}
        ),
        WebRtcProductState::NeedsRelay
    );
    QCOMPARE(
        WebRtcProductPolicy::classifyConnectionFailure(
            EndpointError::ConnectionTimeout,
            EndpointIceState::Checking,
            {"host"}
        ),
        WebRtcProductState::Error
    );

    EndpointConnectionResult connected;
    connected.selectedPair = EndpointCandidatePair {
        "host", "host", "udp", "udp"
    };
    WebRtcProductDiagnostics diagnostics;
    diagnostics.transport.state = EndpointState::Connected;
    diagnostics.media.streamId = 7;
    diagnostics.media.presentedFrames = 1;
    diagnostics.presentedFrameAgeMs = 1'000;
    QVERIFY(WebRtcProductPolicy::hasFreshDirectEvidence(
        connected, diagnostics
    ));
    diagnostics.presentedFrameAgeMs = 1'001;
    QVERIFY(!WebRtcProductPolicy::hasFreshDirectEvidence(
        connected, diagnostics
    ));
}

void WebRtcProductSessionTest::productReceivePath_data()
{
    QTest::addColumn<int>("receiveRole");
    QTest::newRow("receiver-answerer")
        << static_cast<int>(SignalingRole::Answerer);
    QTest::newRow("receiver-offerer")
        << static_cast<int>(SignalingRole::Offerer);
}

void WebRtcProductSessionTest::productReceivePath()
{
    QFETCH(int, receiveRole);
    const SignalingRole receiverRole = static_cast<SignalingRole>(receiveRole);
    QTemporaryDir exchange;
    QVERIFY(exchange.isValid());

    MultiStreamPlaybackManager manager;
    MainWindow window(RendererPreference::Cpu);
    window.resize(800, 520);
    window.show();
    LogManager logs;
    WebRtcProductSessionController controller(
        &window, &manager, &logs, exchange.path()
    );
    QSignalSpy eventSpy(
        &controller, &WebRtcProductSessionController::eventObserved
    );
    auto *startAction =
        window.findChild<QAction *>(QStringLiteral("oneShotWebRtcAction"));
    auto *cancelAction = window.findChild<QAction *>(
        QStringLiteral("cancelOneShotWebRtcAction")
    );
    QVERIFY(startAction != nullptr);
    QVERIFY(cancelAction != nullptr);
    QVERIFY(startAction->isEnabled());
    QVERIFY(!cancelAction->isEnabled());

    WebRtcSessionRequest request;
    request.displayName = QStringLiteral("Week 8 product viewer");
    request.signalingRole = receiverRole;
    QString startError;
    QVERIFY2(controller.start(request, &startError), qPrintable(startError));
    QCOMPARE(controller.state(), WebRtcProductState::Connecting);
    QCOMPARE(window.videoWidgetCount(), 1);
    QCOMPARE(manager.streamCount(), 1);
    QVERIFY(!startAction->isEnabled());
    QVERIFY(cancelAction->isEnabled());

    WebRtcSessionConfig senderConfig;
    senderConfig.signalingRole =
        receiverRole == SignalingRole::Answerer
            ? SignalingRole::Offerer : SignalingRole::Answerer;
    senderConfig.videoDirection = VideoDirection::SendOnly;
    WebRtcEndpointSession sender(senderConfig);
    SessionPackageStore store(exchange.path());
    QCOMPARE(store.prepare(), SessionError::None);

    EndpointConnectionResult senderConnected;
    if (receiverRole == SignalingRole::Answerer) {
        const EndpointDescriptionResult offer = sender.createOffer();
        QVERIFY2(offer.ok(), WebRtcEndpointSession::errorName(offer.error));
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer, QString::fromStdString(offer.sdp)
        );
        QVERIFY(store.write(package).ok());
        const auto answer = waitForPackage(
            store, SessionRole::Answer, package.sessionId
        );
        QVERIFY(answer.has_value());
        senderConnected = sender.acceptAnswerAndWait(
            answer->package.sdp.toStdString()
        );
    } else {
        const auto offer = waitForPackage(store, SessionRole::Offer);
        QVERIFY(offer.has_value());
        const EndpointDescriptionResult answer =
            sender.acceptOfferAndCreateAnswer(
                offer->package.sdp.toStdString()
            );
        QVERIFY2(answer.ok(), WebRtcEndpointSession::errorName(answer.error));
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Answer,
            QString::fromStdString(answer.sdp),
            QDateTime::currentDateTimeUtc(),
            offer->package.sessionId
        );
        QVERIFY(store.write(package).ok());
        senderConnected = sender.waitConnected();
    }
    QVERIFY2(senderConnected.ok(),
             WebRtcEndpointSession::errorName(senderConnected.error));
    QVERIFY(senderConnected.selectedPair.has_value());

    auto port = sender.createSendPort();
    QVERIFY(port.has_value());
    QCOMPARE((*port)(fixedRedIdr(0)), H264SubmitResult::Accepted);
    QTest::qWait(75);
    const H264SubmitResult second = (*port)(fixedRedIdr(33'333));
    QVERIFY(second == H264SubmitResult::Accepted ||
            second == H264SubmitResult::AcceptedAfterDrop);

    QTRY_COMPARE_WITH_TIMEOUT(
        controller.state(), WebRtcProductState::Direct, 10'000
    );
    const WebRtcProductDiagnostics diagnostics =
        controller.diagnosticsSnapshot();
    QVERIFY(diagnostics.selectedNonRelayPair);
    QVERIFY(diagnostics.media.decodedFrames > 0);
    QVERIFY(diagnostics.media.presentedFrames > 0);
    QVERIFY(diagnostics.presentedFrameAgeMs >= 0);
    QVERIFY(diagnostics.presentedFrameAgeMs <= 1'000);
    QVERIFY(!diagnostics.controlAuthorized);
    QVERIFY(!diagnostics.rtmpFallbackStarted);

    QTRY_COMPARE_WITH_TIMEOUT(
        controller.state(), WebRtcProductState::Error, 3'000
    );
    const H264SubmitResult recovered = (*port)(fixedRedIdr(66'666));
    QVERIFY(recovered == H264SubmitResult::Accepted ||
            recovered == H264SubmitResult::AcceptedAfterDrop);
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.state(), WebRtcProductState::Direct, 5'000
    );
    const auto observed = [&eventSpy](WebRtcProductEventKind expected) {
        for (const QList<QVariant> &arguments : eventSpy) {
            if (!arguments.isEmpty() &&
                qvariant_cast<WebRtcProductEvent>(arguments.front()).kind ==
                    expected) {
                return true;
            }
        }
        return false;
    };
    QVERIFY(observed(WebRtcProductEventKind::DirectEstablished));
    QVERIFY(observed(WebRtcProductEventKind::MediaInterrupted));
    QVERIFY(observed(WebRtcProductEventKind::MediaRecovered));

    sender.beginClose();
    sender.close();
    controller.cancel();
    QCOMPARE(controller.state(), WebRtcProductState::Idle);
    QVERIFY(!controller.isActive());
    QCOMPARE(window.videoWidgetCount(), 0);
    QCOMPARE(manager.streamCount(), 0);
    QVERIFY(startAction->isEnabled());
    QVERIFY(!cancelAction->isEnabled());
    QVERIFY(store.managedFiles(SessionRole::Offer).isEmpty());
    QVERIFY(store.managedFiles(SessionRole::Answer).isEmpty());
}

void WebRtcProductSessionTest::cleanupTestCase()
{
    QVERIFY(WebRtcProductSessionController::cleanupGlobal());
}

QTEST_MAIN(WebRtcProductSessionTest)
#include "WebRtcProductSessionTest.moc"
