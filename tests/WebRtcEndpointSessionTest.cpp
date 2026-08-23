#include "webrtc_transport/WebRtcEndpointSession.h"
#include "webrtc_transport/H264ReceivePipeline.h"

#include <QtTest>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <vector>

using namespace rtmp_monitor::webrtc_transport;
using namespace rtmp_monitor::webrtc_transport::detail;

namespace {

H264AccessUnit makeKeyframe(std::int64_t timestampUs = 0)
{
    H264AccessUnit result;
    result.annexB = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xe0, 0x1f,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x06, 0xe2,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00,
    };
    result.mediaTimestampUs = timestampUs;
    result.keyFrame = true;
    return result;
}

bool onlyHostCandidates(const std::vector<std::string> &types)
{
    return !types.empty() && std::all_of(
        types.cbegin(), types.cend(),
        [](const std::string &type) { return type == "host"; }
    );
}

void verifySanitizedSelectedPair(const EndpointConnectionResult &result)
{
    QVERIFY(result.selectedPair.has_value());
    const EndpointCandidatePair &pair = *result.selectedPair;
    const auto safeType = [](const std::string &value) {
        return value == "host" || value == "srflx" || value == "relay";
    };
    QVERIFY(safeType(pair.localType));
    QVERIFY(safeType(pair.remoteType));
    QCOMPARE(pair.localTransport, std::string("udp"));
    QCOMPARE(pair.remoteTransport, std::string("udp"));
    for (const std::string *value : {
             &pair.localType, &pair.remoteType,
             &pair.localTransport, &pair.remoteTransport}) {
        QVERIFY(value->find('.') == std::string::npos);
        QVERIFY(value->find(':') == std::string::npos);
    }
}

void verifyFixedH264Description(const std::string &sdp)
{
    QVERIFY(sdp.find("a=rtpmap:102 H264/90000") != std::string::npos);
    QVERIFY(sdp.find("profile-level-id=42e01f") != std::string::npos);
}

} // namespace

class WebRtcEndpointSessionTest final : public QObject
{
    Q_OBJECT

private slots:
    void allRoleDirectionConfigurationsAreConstructible()
    {
        for (const SignalingRole role : {
                 SignalingRole::Offerer, SignalingRole::Answerer}) {
            for (const VideoDirection direction : {
                     VideoDirection::SendOnly, VideoDirection::ReceiveOnly}) {
                WebRtcSessionConfig config;
                config.signalingRole = role;
                config.videoDirection = direction;
                WebRtcEndpointSession session(config);
                QCOMPARE(session.snapshot().state, EndpointState::New);
                if (role == SignalingRole::Offerer) {
                    QCOMPARE(
                        session.acceptOfferAndCreateAnswer("v=0\r\n").error,
                        EndpointError::InvalidRole
                    );
                } else {
                    QCOMPARE(
                        session.createOffer().error,
                        EndpointError::InvalidRole
                    );
                }
                session.close();
                session.close();
                QCOMPARE(session.snapshot().state, EndpointState::Closed);
            }
        }
    }

    void publisherOffererSendsRealRtp()
    {
        exercisePublisher(SignalingRole::Offerer);
    }

    void publisherAnswererSendsRealRtp()
    {
        exercisePublisher(SignalingRole::Answerer);
    }

    void invalidAuAndExpiredGenerationAreRejected()
    {
        WebRtcSessionConfig config;
        config.signalingRole = SignalingRole::Offerer;
        config.videoDirection = VideoDirection::SendOnly;
        WebRtcEndpointSession session(config);
        const EndpointDescriptionResult offer = session.createOffer();
        QVERIFY2(offer.ok(), WebRtcEndpointSession::errorName(offer.error));
        auto port = session.createSendPort();
        QVERIFY(port.has_value());

        H264AccessUnit invalid;
        QCOMPARE((*port)(std::move(invalid)), H264SubmitResult::InvalidAccessUnit);
        H264AccessUnit oversized = makeKeyframe();
        oversized.annexB.resize(4U * 1024U * 1024U + 1U, 0);
        QCOMPARE(
            (*port)(std::move(oversized)),
            H264SubmitResult::InvalidAccessUnit
        );
        session.beginClose();
        QCOMPARE(
            (*port)(makeKeyframe()),
            H264SubmitResult::InvalidGeneration
        );
        session.close();
    }

    void queueOverflowWaitsForAndRecoversOnKeyframe()
    {
        WebRtcSessionConfig config;
        config.signalingRole = SignalingRole::Offerer;
        config.videoDirection = VideoDirection::SendOnly;
        WebRtcEndpointSession session(config);
        const EndpointDescriptionResult offer = session.createOffer();
        QVERIFY2(offer.ok(), WebRtcEndpointSession::errorName(offer.error));
        auto port = session.createSendPort();
        QVERIFY(port.has_value());

        QCOMPARE((*port)(makeKeyframe(0)), H264SubmitResult::Accepted);
        H264AccessUnit delta = makeKeyframe(33333);
        delta.keyFrame = false;
        QCOMPARE((*port)(delta), H264SubmitResult::Accepted);
        delta.mediaTimestampUs = 66666;
        QCOMPARE((*port)(delta), H264SubmitResult::DroppedCapacity);
        QCOMPARE((*port)(delta), H264SubmitResult::DroppedUntilKeyframe);
        QCOMPARE(
            (*port)(makeKeyframe(100000)),
            H264SubmitResult::AcceptedAfterDrop
        );

        const EndpointSnapshot snapshot = session.snapshot();
        QCOMPARE(snapshot.queueDepth, std::size_t(1));
        QCOMPARE(snapshot.droppedAccessUnits, std::uint64_t(4));
        QVERIFY(!snapshot.waitingForKeyframe);
        session.close();
        QCOMPARE(session.snapshot().queueDepth, std::size_t(0));
    }

    void receiveSinkIsRequiredAndBoundBeforeNegotiation()
    {
        WebRtcSessionConfig receiveConfig;
        receiveConfig.signalingRole = SignalingRole::Offerer;
        receiveConfig.videoDirection = VideoDirection::ReceiveOnly;
        WebRtcEndpointSession receiver(receiveConfig);
        QCOMPARE(
            receiver.createOffer().error,
            EndpointError::MissingReceiveSink
        );
        QCOMPARE(
            receiver.setReceiveSink({}),
            EndpointError::MissingReceiveSink
        );
        QCOMPARE(
            receiver.setReceiveSink([](SessionMediaSample) {
                return H264SubmitResult::Accepted;
            }),
            EndpointError::None
        );
        QVERIFY(receiver.createOffer().ok());
        QCOMPARE(
            receiver.setReceiveSink([](SessionMediaSample) {
                return H264SubmitResult::Accepted;
            }),
            EndpointError::InvalidState
        );
        receiver.close();

        WebRtcSessionConfig sendConfig;
        sendConfig.signalingRole = SignalingRole::Offerer;
        sendConfig.videoDirection = VideoDirection::SendOnly;
        WebRtcEndpointSession sender(sendConfig);
        QCOMPARE(
            sender.setReceiveSink([](SessionMediaSample) {
                return H264SubmitResult::Accepted;
            }),
            EndpointError::InvalidRole
        );
        sender.close();
    }

    void incompatibleH264DescriptionIsRejected()
    {
        WebRtcSessionConfig senderConfig;
        senderConfig.signalingRole = SignalingRole::Offerer;
        senderConfig.videoDirection = VideoDirection::SendOnly;
        WebRtcEndpointSession sender(senderConfig);
        const EndpointDescriptionResult offer = sender.createOffer();
        QVERIFY(offer.ok());

        std::string incompatible = offer.sdp;
        const std::string expected = "profile-level-id=42e01f";
        const std::size_t profile = incompatible.find(expected);
        QVERIFY(profile != std::string::npos);
        incompatible.replace(profile, expected.size(), "profile-level-id=64001f");

        WebRtcSessionConfig receiverConfig;
        receiverConfig.signalingRole = SignalingRole::Answerer;
        receiverConfig.videoDirection = VideoDirection::ReceiveOnly;
        WebRtcEndpointSession receiver(receiverConfig);
        QCOMPARE(
            receiver.setReceiveSink([](SessionMediaSample) {
                return H264SubmitResult::Accepted;
            }),
            EndpointError::None
        );
        QCOMPARE(
            receiver.acceptOfferAndCreateAnswer(incompatible).error,
            EndpointError::IncompatibleMedia
        );
        QCOMPARE(receiver.snapshot().state, EndpointState::New);
        sender.close();
        receiver.close();
    }

    void annexBRecoveryPolicyIsGenerationAndWrapAware()
    {
        H264ReceivePipeline pipeline;
        auto waiting = pipeline.process(
            7,
            {0x00, 0x00, 0x01, 0x67, 0x42, 0xe0, 0x1f},
            0xffffff00U
        );
        QCOMPARE(
            waiting.status,
            H264ReceivePipelineStatus::WaitingForKeyframe
        );
        waiting = pipeline.process(
            7,
            {0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x06, 0xe2},
            0x00000100U
        );
        QCOMPARE(
            waiting.status,
            H264ReceivePipelineStatus::WaitingForKeyframe
        );
        auto ready = pipeline.process(
            7,
            {0x00, 0x00, 0x01, 0x65, 0x88, 0x84},
            0x00000d00U
        );
        QCOMPARE(ready.status, H264ReceivePipelineStatus::Ready);
        QVERIFY(ready.sample.has_value());
        QCOMPARE(ready.sample->generation, std::uint64_t(7));
        QVERIFY(ready.sample->accessUnit.keyFrame);
        QVERIFY(ready.sample->accessUnit.mediaTimestampUs > 0);
        QVERIFY(ready.sample->accessUnit.annexB.size() > 20U);

        ready = pipeline.process(
            7,
            {0x00, 0x00, 0x00, 0x01, 0x41, 0x9a},
            0x00001900U
        );
        QCOMPARE(ready.status, H264ReceivePipelineStatus::Ready);
        QVERIFY(!ready.sample->accessUnit.keyFrame);

        pipeline.resetRecovery();
        waiting = pipeline.process(
            7,
            {0x00, 0x00, 0x00, 0x01, 0x41, 0x9a},
            0x00002500U
        );
        QCOMPARE(
            waiting.status,
            H264ReceivePipelineStatus::WaitingForKeyframe
        );
        waiting = pipeline.process(
            8,
            {0x00, 0x00, 0x00, 0x01, 0x65, 0x88},
            90'000U
        );
        QCOMPARE(
            waiting.status,
            H264ReceivePipelineStatus::WaitingForKeyframe
        );

        auto invalid = pipeline.process(8, {}, 90'001U);
        QCOMPARE(
            invalid.status,
            H264ReceivePipelineStatus::InvalidAccessUnit
        );
        std::vector<std::uint8_t> oversized(
            H264ReceivePipeline::kMaximumAccessUnitBytes + 1U, 0
        );
        invalid = pipeline.process(8, std::move(oversized), 90'002U);
        QCOMPARE(
            invalid.status,
            H264ReceivePipelineStatus::InvalidAccessUnit
        );
    }

    void repeatedCloseIsIdempotent()
    {
        for (int iteration = 0; iteration < 10; ++iteration) {
            WebRtcSessionConfig config;
            config.signalingRole = SignalingRole::Offerer;
            config.videoDirection = VideoDirection::ReceiveOnly;
            WebRtcEndpointSession session(config);
            session.close();
            session.close();
            QCOMPARE(session.snapshot().state, EndpointState::Closed);
        }
    }

    void cleanupTestCase()
    {
        auto cleanup = rtc::Cleanup();
        QVERIFY(cleanup.wait_for(std::chrono::seconds(10)) !=
                std::future_status::timeout);
        cleanup.get();
    }

private:
    static void exercisePublisher(SignalingRole publisherRole)
    {
        WebRtcSessionConfig senderConfig;
        senderConfig.signalingRole = publisherRole;
        senderConfig.videoDirection = VideoDirection::SendOnly;
        WebRtcSessionConfig receiverConfig;
        receiverConfig.signalingRole =
            publisherRole == SignalingRole::Offerer
                ? SignalingRole::Answerer
                : SignalingRole::Offerer;
        receiverConfig.videoDirection = VideoDirection::ReceiveOnly;

        WebRtcEndpointSession sender(senderConfig);
        WebRtcEndpointSession receiver(receiverConfig);
        std::atomic_uint64_t received {0};
        QCOMPARE(
            receiver.setReceiveSink([&received](SessionMediaSample sample) {
                if (sample.generation != 0 &&
                    isValidH264AccessUnit(
                        sample.accessUnit,
                        H264ReceivePipeline::kMaximumAccessUnitBytes
                    )) {
                    received.fetch_add(1, std::memory_order_relaxed);
                    return H264SubmitResult::Accepted;
                }
                return H264SubmitResult::InvalidAccessUnit;
            }),
            EndpointError::None
        );
        EndpointConnectionResult senderConnected;
        EndpointConnectionResult receiverConnected;

        if (publisherRole == SignalingRole::Offerer) {
            const EndpointDescriptionResult offer = sender.createOffer();
            QVERIFY2(offer.ok(), WebRtcEndpointSession::errorName(offer.error));
            verifyFixedH264Description(offer.sdp);
            const EndpointDescriptionResult answer =
                receiver.acceptOfferAndCreateAnswer(offer.sdp);
            QVERIFY2(answer.ok(), WebRtcEndpointSession::errorName(answer.error));
            senderConnected = sender.acceptAnswerAndWait(answer.sdp);
            receiverConnected = receiver.waitConnected();
        } else {
            const EndpointDescriptionResult offer = receiver.createOffer();
            QVERIFY2(offer.ok(), WebRtcEndpointSession::errorName(offer.error));
            verifyFixedH264Description(offer.sdp);
            const EndpointDescriptionResult answer =
                sender.acceptOfferAndCreateAnswer(offer.sdp);
            QVERIFY2(answer.ok(), WebRtcEndpointSession::errorName(answer.error));
            receiverConnected = receiver.acceptAnswerAndWait(answer.sdp);
            senderConnected = sender.waitConnected();
        }

        QVERIFY2(
            senderConnected.ok(),
            WebRtcEndpointSession::errorName(senderConnected.error)
        );
        QVERIFY2(
            receiverConnected.ok(),
            WebRtcEndpointSession::errorName(receiverConnected.error)
        );
        QVERIFY(onlyHostCandidates(senderConnected.candidateTypes));
        QVERIFY(onlyHostCandidates(receiverConnected.candidateTypes));
        verifySanitizedSelectedPair(senderConnected);
        verifySanitizedSelectedPair(receiverConnected);

        QTRY_VERIFY_WITH_TIMEOUT(sender.snapshot().trackOpen, 5000);
        auto port = sender.createSendPort();
        QVERIFY(port.has_value());
        const H264SubmitResult submitted = (*port)(makeKeyframe(1000));
        QVERIFY(submitted == H264SubmitResult::Accepted ||
                submitted == H264SubmitResult::AcceptedAfterDrop);
        QTRY_VERIFY_WITH_TIMEOUT(sender.snapshot().sentAccessUnits >= 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(receiver.snapshot().receivedRtpPackets >= 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            receiver.snapshot().submittedAccessUnits >= 1,
            5000
        );
        QCOMPARE(received.load(std::memory_order_relaxed), std::uint64_t(1));
        QCOMPARE(receiver.snapshot().invalidAccessUnits, std::uint64_t(0));

        sender.beginClose();
        QCOMPARE(
            (*port)(makeKeyframe(2000)),
            H264SubmitResult::InvalidGeneration
        );
        sender.close();
        sender.close();
        QTRY_COMPARE_WITH_TIMEOUT(
            receiver.snapshot().state,
            EndpointState::Failed,
            35000
        );
        const std::uint64_t receivedBeforeClose =
            received.load(std::memory_order_relaxed);
        QTest::qWait(250);
        QCOMPARE(
            received.load(std::memory_order_relaxed),
            receivedBeforeClose
        );
        receiver.close();
        receiver.close();
        QCOMPARE(sender.snapshot().queueDepth, std::size_t(0));
    }
};

QTEST_GUILESS_MAIN(WebRtcEndpointSessionTest)
#include "WebRtcEndpointSessionTest.moc"
