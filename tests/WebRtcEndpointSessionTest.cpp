#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QtTest>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <vector>

using namespace rtmp_monitor::webrtc_transport;

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

        QTRY_VERIFY_WITH_TIMEOUT(sender.snapshot().trackOpen, 5000);
        auto port = sender.createSendPort();
        QVERIFY(port.has_value());
        const H264SubmitResult submitted = (*port)(makeKeyframe(1000));
        QVERIFY(submitted == H264SubmitResult::Accepted ||
                submitted == H264SubmitResult::AcceptedAfterDrop);
        QTRY_VERIFY_WITH_TIMEOUT(sender.snapshot().sentAccessUnits >= 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(receiver.snapshot().receivedRtpPackets >= 1, 5000);

        sender.beginClose();
        QCOMPARE(
            (*port)(makeKeyframe(2000)),
            H264SubmitResult::InvalidGeneration
        );
        sender.close();
        sender.close();
        receiver.close();
        receiver.close();
        QCOMPARE(sender.snapshot().queueDepth, std::size_t(0));
    }
};

QTEST_GUILESS_MAIN(WebRtcEndpointSessionTest)
#include "WebRtcEndpointSessionTest.moc"
