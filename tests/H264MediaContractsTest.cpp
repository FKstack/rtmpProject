#include <QTest>

#include <array>
#include <type_traits>

#include "h264/H264MediaContracts.h"
#include "webrtc_contracts/WebRtcSessionContracts.h"

class H264MediaContractsTest final : public QObject
{
    Q_OBJECT

private slots:
    void accessUnitValidationIsStrictAndBounded();
    void signalingRoleAndMediaDirectionAreOrthogonal();
    void sessionConfigurationIsRuntimeOnly();
};

void H264MediaContractsTest::accessUnitValidationIsStrictAndBounded()
{
    H264AccessUnit accessUnit;
    accessUnit.annexB = {0, 0, 0, 1, 0x65, 0x01};
    accessUnit.mediaTimestampUs = 33'333;
    accessUnit.keyFrame = true;

    QVERIFY(isValidH264AccessUnit(accessUnit, accessUnit.annexB.size()));
    QVERIFY(!isValidH264AccessUnit(accessUnit, accessUnit.annexB.size() - 1));

    accessUnit.annexB = {0, 0, 1, 0x41};
    accessUnit.keyFrame = false;
    QVERIFY(isValidH264AccessUnit(accessUnit, accessUnit.annexB.size()));

    accessUnit.annexB = {0, 0, 2, 0x41};
    QVERIFY(!isValidH264AccessUnit(accessUnit, accessUnit.annexB.size()));
    accessUnit.annexB = {0, 0, 1};
    QVERIFY(!isValidH264AccessUnit(accessUnit, accessUnit.annexB.size()));
    accessUnit.annexB.clear();
    QVERIFY(!isValidH264AccessUnit(accessUnit, 1));
    accessUnit.annexB = {0, 0, 1, 0x41};
    accessUnit.mediaTimestampUs = -1;
    QVERIFY(!isValidH264AccessUnit(accessUnit, accessUnit.annexB.size()));
}

void H264MediaContractsTest::signalingRoleAndMediaDirectionAreOrthogonal()
{
    const std::array<WebRtcSessionConfig, 4> configurations {{
        {SignalingRole::Offerer, VideoDirection::SendOnly, {}},
        {SignalingRole::Answerer, VideoDirection::SendOnly, {}},
        {SignalingRole::Offerer, VideoDirection::ReceiveOnly, {}},
        {SignalingRole::Answerer, VideoDirection::ReceiveOnly, {}},
    }};

    QCOMPARE(configurations.size(), std::size_t {4});
    QCOMPARE(
        configurations[2].signalingRole,
        SignalingRole::Offerer
    );
    QCOMPARE(
        configurations[2].videoDirection,
        VideoDirection::ReceiveOnly
    );
    static_assert(!std::is_convertible_v<SignalingRole, VideoDirection>);
}

void H264MediaContractsTest::sessionConfigurationIsRuntimeOnly()
{
    WebRtcSessionConfig config;
    config.signalingRole = SignalingRole::Answerer;
    config.videoDirection = VideoDirection::ReceiveOnly;
    config.ice.servers.push_back({
        {"turn:runtime.invalid"}, "ephemeral-user", "ephemeral-password"
    });

    QCOMPARE(config.ice.servers.size(), std::size_t {1});
    QCOMPARE(config.ice.servers.front().urls.size(), std::size_t {1});
    QCOMPARE(config.signalingRole, SignalingRole::Answerer);
    QCOMPARE(config.videoDirection, VideoDirection::ReceiveOnly);
}

QTEST_GUILESS_MAIN(H264MediaContractsTest)

#include "H264MediaContractsTest.moc"
