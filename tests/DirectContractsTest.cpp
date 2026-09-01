#include "identity_contracts/IdentityContracts.h"
#include "runtime_config/MqttRuntimeConfig.h"
#include "signaling_contracts/SignalingContracts.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

using namespace rtmp::p2p;

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "direct_contract_test_failed:" << message << '\n';
        std::exit(1);
    }
}

std::string validOffer()
{
    return R"({"schemaVersion":1,"messageId":"123e4567-e89b-42d3-a456-426614174000","messageType":"webrtc.offer","sentAtUtc":"2026-09-01T00:00:00.000Z","ttlMs":30000,"sourceIdentity":{"kind":"operator","id":"user-1"},"sourceClientInstanceId":"desktop-1","targetIdentity":{"kind":"device","id":"device-1"},"sessionId":"123e4567-e89b-42d3-a456-426614174001","attemptId":"123e4567-e89b-42d3-a456-426614174002","correlationId":"123e4567-e89b-42d3-a456-426614174003","sequence":1,"sessionNonce":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","payload":{"sdp":"v=0"}})";
}

std::string readVector(const char *name)
{
    std::ifstream input(std::string(RTMP_MONITOR_SIGNALING_VECTOR_DIR) + "/" + name,
                        std::ios::binary);
    require(input.good(), "shared vector open");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}
}

int main()
{
    const auto device = DeviceId::parse("device-1");
    const auto user = UserId::parse("user_1");
    const auto instance = ClientInstanceId::parse("desktop.1");
    require(device && user && instance, "valid opaque IDs");
    require(!DeviceId::parse("bad/device") && !DeviceId::parse("bad+device")
            && !DeviceId::parse("bad#device"), "reserved ID characters");
    require(MessageId::parse("123e4567-e89b-42d3-a456-426614174000").has_value(), "uuid v4");
    require(!MessageId::parse("123E4567-E89B-42D3-A456-426614174000"), "canonical lowercase uuid");
    require(MqttClientIdCodec::device(*device, MqttPlane::Signal) == "device-device-1-signal", "device client id");
    require(MqttClientIdCodec::operatorClient(*user, *instance, MqttPlane::Control)
            == "operator-user_1-desktop.1-control", "operator client id");

    TopicRoute signal{TopicKind::SignalToDevice, "device-1", {}, "user_1", "desktop.1"};
    const auto topic = TopicCodec::encode(signal);
    require(topic && TopicCodec::decode(*topic).has_value(), "topic round trip");
    require(!TopicCodec::decode("rtmp-monitor/v1/signaling/#"), "wildcard topic");
    require(!TopicCodec::decode("device/control"), "legacy topic excluded");
    AclContext view{false, "device-1", "target-1", "user_1", "desktop.1", ProvisioningScope::View};
    require(AclPolicy::authorize(view, Access::Publish, *topic).ok, "view signaling publish");
    TopicRoute command{TopicKind::ControlCommand, {}, "target-1", "user_1", "desktop.1"};
    const auto commandTopic = TopicCodec::encode(command);
    require(commandTopic && !AclPolicy::authorize(view, Access::Publish, *commandTopic).ok, "view control rejected");
    view.scope = ProvisioningScope::Control;
    require(AclPolicy::authorize(view, Access::Publish, *commandTopic).ok, "control scope command");

    auto decoded = EnvelopeCodec::decode(validOffer());
    require(decoded.validation.ok && decoded.envelope, "valid envelope");
    require(EnvelopeCodec::encode(*decoded.envelope).has_value(), "envelope round trip");
    auto duplicate = validOffer();
    duplicate.insert(1, "\"schemaVersion\":1,");
    require(EnvelopeCodec::decode(duplicate).validation.error == ContractError::DuplicateKey, "duplicate key");
    auto exponent = validOffer();
    exponent.replace(exponent.find("\"ttlMs\":30000"), 13, "\"ttlMs\":3e4");
    require(EnvelopeCodec::decode(exponent).validation.error == ContractError::InvalidInteger, "exponent rejected");
    auto unknown = validOffer();
    unknown.insert(unknown.size() - 1, ",\"extra\":1");
    require(EnvelopeCodec::decode(unknown).validation.error == ContractError::UnknownField, "unknown field");
    require(EnvelopeCodec::decode(readVector("golden_offer.json")).validation.ok, "shared golden envelope");
    require(EnvelopeCodec::decode(readVector("invalid_duplicate_key.json")).validation.error
            == ContractError::DuplicateKey, "shared duplicate vector");
    require(EnvelopeCodec::decode(readVector("invalid_exponent.json")).validation.error
            == ContractError::InvalidInteger, "shared exponent vector");
    const auto tooLargeInteger = std::string(R"({"value":9007199254740992})");
    require(EnvelopeCodec::decode(tooLargeInteger).validation.error == ContractError::InvalidInteger,
            "2^53 rejected before schema");

    const auto aclDocument = QJsonDocument::fromJson(QByteArray::fromStdString(readVector("acl_vectors.json")));
    require(aclDocument.isObject(), "shared acl vector parse");
    const auto aclRoot = aclDocument.object();
    AclContext vectorContext{false, "device-1", "target-1", "operator-1", "desktop-1",
                             ProvisioningScope::View};
    for (const auto entryValue : aclRoot["operatorPublish"].toArray()) {
        const auto entry = entryValue.toObject();
        const auto vectorTopic = entry["topic"].toString().toStdString();
        vectorContext.scope = ProvisioningScope::View;
        require(AclPolicy::authorize(vectorContext, Access::Publish, vectorTopic).ok == entry["view"].toBool(),
                "shared view acl vector");
        vectorContext.scope = ProvisioningScope::Control;
        require(AclPolicy::authorize(vectorContext, Access::Publish, vectorTopic).ok == entry["control"].toBool(),
                "shared control acl vector");
    }

    require(TemporalPolicy::validate(1000, 3000, 2000).ok, "ttl valid");
    require(TemporalPolicy::validate(1000, 500, 2000).error == ContractError::Expired, "ttl expired");
    require(TemporalPolicy::validate(200001, 1000, 0).error == ContractError::ClockSkew, "clock skew");
    ReplayGuard replay;
    require(replay.remember("operator:user_1", "m1", 20, 0).ok, "replay first");
    require(replay.remember("operator:user_1", "m1", 20, 1).error == ContractError::Replay, "replay duplicate");
    TombstoneStore tombstones;
    tombstones.close("s", "a", "n", 0);
    require(tombstones.accept("s", "a", "n", 1).error == ContractError::Tombstoned, "tombstone");
    AckTracker ack;
    ack.start("m", 0);
    require(ack.poll("m", 3000) == AckAction::Retry && ack.poll("m", 6000) == AckAction::TimedOut, "ack retry timeout");
    require(SessionTransitionPolicy::validate(SessionState::Idle, SessionState::Requested).ok, "session transition");
    require(!SessionTransitionPolicy::validate(SessionState::Idle, SessionState::Connected).ok, "invalid transition");

    CandidatePolicy candidates;
    require(candidates.accept({"candidate:1 1 UDP 1 192.0.2.1 9 typ host", "0", false}).ok, "candidate first");
    require(candidates.accept({"candidate:1 1 UDP 1 192.0.2.1 9 typ host", "0", false}).error == ContractError::DuplicateCandidate, "candidate duplicate");
    require(candidates.accept({{}, {}, true}).ok, "candidate eoc");
    require(candidates.accept({"candidate:2", "0", false}).error == ContractError::CandidateAfterEnd, "candidate after eoc");
    CandidatePolicy capacity;
    for (int i = 0; i < 64; ++i)
        require(capacity.accept({"candidate:" + std::to_string(i), "0", false}).ok, "candidate capacity 64");
    require(capacity.accept({"candidate:65", "0", false}).error == ContractError::LimitExceeded,
            "candidate capacity 65");

    MqttRuntimeConfig defaults;
    require(validateMqttRuntimeConfig(defaults).ok, "safe defaults");
    defaults.brokerHostname = "example.invalid";
    require(!validateMqttRuntimeConfig(defaults).ok, "disabled endpoint rejected");
    MqttRuntimeConfig anonymous;
    anonymous.enabled = true;
    anonymous.brokerHostname = "broker.invalid";
    anonymous.brokerPort = 1883;
    anonymous.signalClientId = "operator-user-desktop-signal";
    require(validateMqttRuntimeConfig(anonymous).ok,
            "explicit anonymous runtime accepted");
    anonymous.signalCredentialReference = "credential-ref";
    require(validateMqttRuntimeConfig(anonymous).error
                == "anonymous_config_contains_credential",
            "anonymous runtime rejects credential reference");
    MqttRuntimeConfig viewConfig;
    viewConfig.authorizedDevices.push_back({"device-1", "target-1", DeviceScope::View});
    viewConfig.controlClientId = "operator-user-desktop-control";
    require(validateMqttRuntimeConfig(viewConfig).error == "view_scope_has_control_credential", "view control client rejected");

    std::cout << "direct_contracts_passed\n";
    return 0;
}
