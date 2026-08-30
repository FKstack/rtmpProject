#include "logging/LogManager.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/MainWindow.h"
#include "ui/VideoCanvasHost.h"
#include "ui/VideoGridWidget.h"
#include "webrtc_dev/SessionPackage.h"
#include "webrtc_product/WebRtcProductSessionController.h"
#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QAction>
#include <QByteArray>
#include <QElapsedTimer>
#include <QDir>
#include <QTemporaryDir>
#include <QProcessEnvironment>
#include <QtTest>

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

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
    void fourSessionsAreIsolatedAndCapacityIsStable();
    void fourPeerMediaSessionsRemainIsolated();
    void synchronousSignalReentryIsSafe();
    void immediateCancelDuringGridAnimationEventuallyRemovesWidget();
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
    StreamId createdStreamId = kInvalidStreamId;
    QVERIFY2(controller.start(request, &startError, &createdStreamId),
             qPrintable(startError));
    QVERIFY(createdStreamId != kInvalidStreamId);
    QCOMPARE(controller.state(), WebRtcProductState::Connecting);
    QCOMPARE(window.videoWidgetCount(), 1);
    QCOMPARE(manager.streamCount(), 1);
    QVERIFY(startAction->isEnabled());
    QVERIFY(cancelAction->isEnabled());

    WebRtcSessionConfig senderConfig;
    senderConfig.signalingRole =
        receiverRole == SignalingRole::Answerer
            ? SignalingRole::Offerer : SignalingRole::Answerer;
    senderConfig.videoDirection = VideoDirection::SendOnly;
    WebRtcEndpointSession sender(senderConfig);
    SessionPackageStore store(controller.exchangeRoot(createdStreamId));
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

void WebRtcProductSessionTest::fourSessionsAreIsolatedAndCapacityIsStable()
{
    QTemporaryDir exchange;
    QVERIFY(exchange.isValid());
    MultiStreamPlaybackManager manager;
    MainWindow window(RendererPreference::Cpu);
    LogManager logs;
    WebRtcProductSessionController controller(
        &window, &manager, &logs, exchange.path()
    );
    std::vector<StreamId> ids;
    for (int index = 0; index < 4; ++index) {
        WebRtcSessionRequest request;
        request.displayName = QStringLiteral("session-%1").arg(index + 1);
        StreamId streamId = kInvalidStreamId;
        QString error;
        QVERIFY2(controller.start(request, &error, &streamId),
                 qPrintable(error));
        ids.push_back(streamId);
        QCOMPARE(
            controller.exchangeRoot(streamId),
            QDir(exchange.path()).filePath(
                QStringLiteral("session-%1").arg(
                    index + 1, 2, 10, QLatin1Char('0')
                )
            )
        );
    }
    QCOMPARE(controller.activeStreamIds(), ids);
    QCOMPARE(controller.diagnosticsSnapshots().size(), std::size_t(4));
    QCOMPARE(controller.diagnosticsSnapshot().streamId, kInvalidStreamId);
    QCOMPARE(window.videoWidgetCount(), 4);
    QCOMPARE(manager.streamCount(), 4);
    const std::vector<StreamId> beforeFifthIds = controller.activeStreamIds();
    std::vector<std::uint64_t> beforeFifthGenerations;
    for (const StreamId streamId : beforeFifthIds) {
        beforeFifthGenerations.push_back(
            controller.diagnosticsSnapshot(streamId).mediaGeneration
        );
    }

    WebRtcSessionRequest fifth;
    fifth.displayName = QStringLiteral("fifth");
    QString capacityError;
    QVERIFY(!controller.start(fifth, &capacityError));
    QCOMPARE(capacityError, QStringLiteral("capacity_reached"));
    QCOMPARE(controller.activeStreamIds(), beforeFifthIds);
    QCOMPARE(window.videoWidgetCount(), 4);
    QCOMPARE(manager.streamCount(), 4);
    for (std::size_t index = 0; index < beforeFifthIds.size(); ++index) {
        QCOMPARE(
            controller.diagnosticsSnapshot(beforeFifthIds[index])
                .mediaGeneration,
            beforeFifthGenerations[index]
        );
    }

    QElapsedTimer stopped;
    stopped.start();
    controller.cancel(ids[1]);
    QVERIFY(stopped.elapsed() < 1'000);
    QVERIFY(!controller.isActive(ids[1]));
    for (const StreamId streamId : {ids[0], ids[2], ids[3]}) {
        QVERIFY(controller.isActive(streamId));
    }
    QCOMPARE(window.videoWidgetCount(), 3);
    QCOMPARE(manager.streamCount(), 3);

    StreamId rebuilt = kInvalidStreamId;
    QString error;
    QVERIFY(controller.start(fifth, &error, &rebuilt));
    QVERIFY(rebuilt != ids[1]);
    QVERIFY(controller.exchangeRoot(rebuilt).endsWith(
        QStringLiteral("session-02")
    ));
    controller.cancel();
    QVERIFY(!controller.isActive());
    QCOMPARE(window.videoWidgetCount(), 0);
    QCOMPARE(manager.streamCount(), 0);
}

void WebRtcProductSessionTest::fourPeerMediaSessionsRemainIsolated()
{
    QTemporaryDir exchange;
    QVERIFY(exchange.isValid());
    MultiStreamPlaybackManager manager;
    MainWindow window(RendererPreference::Cpu);
    window.resize(960, 640);
    window.show();
    LogManager logs;
    WebRtcProductSessionController controller(
        &window, &manager, &logs, exchange.path()
    );
    std::vector<StreamId> ids;
    std::vector<std::unique_ptr<WebRtcEndpointSession>> senders;
    std::vector<H264SubmitPort> ports;
    auto connectSender = [&](StreamId streamId) {
        WebRtcSessionConfig config;
        config.signalingRole = SignalingRole::Offerer;
        config.videoDirection = VideoDirection::SendOnly;
        auto sender = std::make_unique<WebRtcEndpointSession>(config);
        SessionPackageStore store(controller.exchangeRoot(streamId));
        QCOMPARE(store.prepare(), SessionError::None);
        const EndpointDescriptionResult offer = sender->createOffer();
        QVERIFY2(offer.ok(), WebRtcEndpointSession::errorName(offer.error));
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer, QString::fromStdString(offer.sdp)
        );
        QVERIFY(store.write(package).ok());
        const auto answer = waitForPackage(
            store, SessionRole::Answer, package.sessionId
        );
        QVERIFY(answer.has_value());
        const EndpointConnectionResult connected =
            sender->acceptAnswerAndWait(answer->package.sdp.toStdString());
        QVERIFY2(connected.ok(),
                 WebRtcEndpointSession::errorName(connected.error));
        auto port = sender->createSendPort();
        QVERIFY(port.has_value());
        ports.push_back(std::move(*port));
        senders.push_back(std::move(sender));
    };
    for (int index = 0; index < 4; ++index) {
        WebRtcSessionRequest request;
        request.displayName = QStringLiteral("peer-%1").arg(index + 1);
        StreamId streamId = kInvalidStreamId;
        QString error;
        QVERIFY2(
            controller.start(request, &error, &streamId),
            qPrintable(QStringLiteral("index=%1 error=%2")
                .arg(index).arg(error))
        );
        ids.push_back(streamId);
        connectSender(streamId);
        QTest::qWait(600);
    }
    for (std::size_t index = 0; index < ports.size(); ++index) {
        QCOMPARE(ports[index](fixedRedIdr(0)), H264SubmitResult::Accepted);
        QTest::qWait(40);
        (void)ports[index](fixedRedIdr(33'333));
    }
    for (const StreamId streamId : ids) {
        QTRY_COMPARE_WITH_TIMEOUT(
            controller.state(streamId), WebRtcProductState::Direct, 10'000
        );
    }
    std::uint64_t before[3] {};
    for (int index = 1; index < 4; ++index) {
        before[index - 1] = controller.diagnosticsSnapshot(
            ids[static_cast<std::size_t>(index)]
        ).media.presentedFrames;
    }
    senders[0]->beginClose();
    senders[0]->close();
    for (int frame = 0; frame < 12; ++frame) {
        for (std::size_t index = 1; index < ports.size(); ++index) {
            (void)ports[index](fixedRedIdr(66'666 + frame * 33'333));
        }
        QTest::qWait(35);
    }
    for (int index = 1; index < 4; ++index) {
        QVERIFY(controller.diagnosticsSnapshot(
            ids[static_cast<std::size_t>(index)]
        ).media.presentedFrames > before[index - 1]);
    }
    QElapsedTimer stopTimer;
    stopTimer.start();
    controller.cancel(ids[0]);
    QVERIFY(stopTimer.elapsed() < 1'000);

    WebRtcSessionRequest rebuiltRequest;
    rebuiltRequest.displayName = QStringLiteral("peer-rebuilt");
    StreamId rebuilt = kInvalidStreamId;
    QString rebuiltError;
    QVERIFY(controller.start(rebuiltRequest, &rebuiltError, &rebuilt));
    connectSender(rebuilt);
    const std::size_t rebuiltPort = ports.size() - 1;
    QCOMPARE(ports[rebuiltPort](fixedRedIdr(0)), H264SubmitResult::Accepted);
    QTest::qWait(40);
    (void)ports[rebuiltPort](fixedRedIdr(33'333));
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.state(rebuilt), WebRtcProductState::Direct, 10'000
    );

    bool durationOk = false;
    int durationSeconds = qEnvironmentVariableIntValue(
        "RTMP_MONITOR_W9_SMOKE_SECONDS", &durationOk
    );
    if (!durationOk || durationSeconds < 1) durationSeconds = 1;
    QElapsedTimer duration;
    duration.start();
    std::int64_t timestamp = 100'000;
    bool longFaultStopped = false;
    bool longFaultRebuilt = false;
    std::size_t longRebuiltPort = 0;
    std::vector<std::pair<StreamId,std::uint64_t>> longFaultContinuity;
    while (duration.elapsed() < durationSeconds * 1'000) {
        const qint64 elapsed = duration.elapsed();
        if (durationSeconds >= 900 && !longFaultStopped &&
            elapsed >= 600'000) {
            controller.cancel(ids[1]);
            senders[1]->beginClose();
            senders[1]->close();
            for (const StreamId streamId : {ids[2], ids[3], rebuilt}) {
                longFaultContinuity.emplace_back(
                    streamId,
                    controller.diagnosticsSnapshot(streamId)
                        .media.presentedFrames
                );
            }
            longFaultStopped = true;
        }
        if (durationSeconds >= 900 && longFaultStopped &&
            !longFaultRebuilt && elapsed >= 720'000) {
            for (const auto &[streamId, presentedBefore] :
                 longFaultContinuity) {
                QVERIFY(controller.diagnosticsSnapshot(streamId)
                    .media.presentedFrames > presentedBefore);
            }
            WebRtcSessionRequest longRequest;
            longRequest.displayName = QStringLiteral("peer-minute-12");
            StreamId longStream = kInvalidStreamId;
            QString longError;
            QVERIFY2(controller.start(longRequest, &longError, &longStream),
                     qPrintable(longError));
            connectSender(longStream);
            longRebuiltPort = ports.size() - 1;
            (void)ports[longRebuiltPort](fixedRedIdr(timestamp));
            QTest::qWait(40);
            (void)ports[longRebuiltPort](fixedRedIdr(timestamp + 33'333));
            QTRY_COMPARE_WITH_TIMEOUT(
                controller.state(longStream), WebRtcProductState::Direct,
                10'000
            );
            longFaultRebuilt = true;
        }
        for (std::size_t index = 1; index < 4; ++index) {
            if (index == 1 && longFaultStopped) continue;
            (void)ports[index](fixedRedIdr(timestamp));
        }
        (void)ports[rebuiltPort](fixedRedIdr(timestamp));
        if (longFaultRebuilt) {
            (void)ports[longRebuiltPort](fixedRedIdr(timestamp));
        }
        timestamp += 33'333;
        QTest::qWait(100);
    }
    for (const WebRtcProductDiagnostics &diagnostics :
         controller.diagnosticsSnapshots()) {
        QVERIFY(diagnostics.transport.queueDepth <= 2U);
        QVERIFY(diagnostics.media.queuePackets <= 45);
        QVERIFY(diagnostics.media.queueBytes <= 4 * 1024 * 1024);
    }
    for (auto &sender : senders) {
        sender->beginClose();
        sender->close();
    }
    controller.cancel();
    QCOMPARE(manager.streamCount(), 0);
    QCOMPARE(window.videoWidgetCount(), 0);
}

void WebRtcProductSessionTest::synchronousSignalReentryIsSafe()
{
    QTemporaryDir exchange;
    QVERIFY(exchange.isValid());
    MultiStreamPlaybackManager manager;
    MainWindow window(RendererPreference::Cpu);
    LogManager logs;
    WebRtcProductSessionController controller(
        &window, &manager, &logs, exchange.path()
    );

    bool cancelledFromState = false;
    connect(
        &controller,
        &WebRtcProductSessionController::streamStateChanged,
        &controller,
        [&](StreamId streamId, WebRtcProductState value) {
            if (!cancelledFromState &&
                value == WebRtcProductState::Connecting) {
                cancelledFromState = true;
                controller.cancel(streamId);
            }
        }
    );
    WebRtcSessionRequest first;
    first.displayName = QStringLiteral("state-reentry");
    QString error;
    QVERIFY2(controller.start(first, &error), qPrintable(error));
    QVERIFY(cancelledFromState);
    QVERIFY(!controller.isActive());
    QCOMPARE(manager.streamCount(), 0);

    bool cancelledFromDiagnostics = false;
    connect(
        &controller,
        &WebRtcProductSessionController::diagnosticsChanged,
        &controller,
        [&](const WebRtcProductDiagnostics &diagnostics) {
            if (!cancelledFromDiagnostics) {
                cancelledFromDiagnostics = true;
                controller.cancel(diagnostics.streamId);
            }
        }
    );
    WebRtcSessionRequest second;
    second.displayName = QStringLiteral("diagnostics-reentry");
    QVERIFY2(controller.start(second, &error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(cancelledFromDiagnostics, 2'000);
    QVERIFY(!controller.isActive());
    QCOMPARE(manager.streamCount(), 0);

    bool recursiveCancelled = false;
    bool startRejectedWhileClosingAll = false;
    connect(
        &controller,
        &WebRtcProductSessionController::eventObserved,
        &controller,
        [&](const WebRtcProductEvent &event) {
            if (event.kind == WebRtcProductEventKind::Cancelled &&
                !recursiveCancelled) {
                recursiveCancelled = true;
                WebRtcSessionRequest rejected;
                rejected.displayName = QStringLiteral("closing-all-reentry");
                QString rejectedError;
                startRejectedWhileClosingAll =
                    !controller.start(rejected, &rejectedError) &&
                    rejectedError == QStringLiteral("closing_all");
                controller.cancel(event.streamId);
                controller.cancel();
            }
        }
    );
    WebRtcSessionRequest third;
    third.displayName = QStringLiteral("cancelled-reentry");
    QVERIFY2(controller.start(third, &error), qPrintable(error));
    controller.cancel();
    QVERIFY(recursiveCancelled);
    QVERIFY(startRejectedWhileClosingAll);
    QVERIFY(!controller.isActive());
    QCOMPARE(manager.streamCount(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(window.videoWidgetCount(), 0, 2'000);
}

void WebRtcProductSessionTest::
immediateCancelDuringGridAnimationEventuallyRemovesWidget()
{
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
    std::vector<StreamId> ids;
    for (int index = 0; index < 2; ++index) {
        WebRtcSessionRequest request;
        request.displayName = QStringLiteral("animation-%1").arg(index);
        StreamId streamId = kInvalidStreamId;
        QString error;
        QVERIFY2(controller.start(request, &error, &streamId),
                 qPrintable(error));
        ids.push_back(streamId);
        QTest::qWait(400);
    }
    auto *grid = window.findChild<VideoGridWidget *>();
    QVERIFY(grid != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(!grid->isSwapAnimationInProgress(), 2'000);
    QVERIFY(grid->swapVideoWidgets(0, 1));
    QVERIFY(grid->isSwapAnimationInProgress());
    controller.cancel(ids.front());
    QVERIFY(!controller.isActive(ids.front()));
    QCOMPARE(manager.streamCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(window.videoWidgetCount(), 1, 2'000);
    controller.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(window.videoWidgetCount(), 0, 2'000);
}

void WebRtcProductSessionTest::cleanupTestCase()
{
    QVERIFY(WebRtcProductSessionController::cleanupGlobal());
}

QTEST_MAIN(WebRtcProductSessionTest)
#include "WebRtcProductSessionTest.moc"
