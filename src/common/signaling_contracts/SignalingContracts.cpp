#include "signaling_contracts/SignalingContracts.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>

namespace rtmp::p2p {
namespace {

ValidationResult failure(ContractError error) { return {false, error}; }
ValidationResult success() { return {true, ContractError::None}; }

bool validOpaque(std::string_view value) { return DeviceId::parse(value).has_value(); }
bool validUuid(std::string_view value) { return MessageId::parse(value).has_value(); }

std::vector<std::string> split(std::string_view text)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('/', start);
        result.emplace_back(text.substr(start, end == std::string_view::npos
                                                   ? text.size() - start
                                                   : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

void appendUtf8(std::string &out, unsigned value)
{
    if (value <= 0x7f) out.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (value >> 6)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xe0 | (value >> 12)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

class JsonScanner final {
public:
    explicit JsonScanner(std::string_view input) : input_(input) {}
    ValidationResult scan()
    {
        skipSpace();
        auto result = value(1);
        skipSpace();
        if (result.ok && pos_ != input_.size()) return failure(ContractError::InvalidJson);
        return result;
    }

private:
    void skipSpace()
    {
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\n'
               || input_[pos_] == '\r' || input_[pos_] == '\t')) ++pos_;
    }
    bool take(char expected)
    {
        if (pos_ >= input_.size() || input_[pos_] != expected) return false;
        ++pos_;
        return true;
    }
    ValidationResult value(int depth)
    {
        if (depth > 8 || pos_ >= input_.size()) return failure(depth > 8 ? ContractError::LimitExceeded : ContractError::InvalidJson);
        switch (input_[pos_]) {
        case '{': return object(depth);
        case '[': return array(depth);
        case '"': { std::string ignored; return string(ignored); }
        case 't': return literal("true");
        case 'f': return literal("false");
        case 'n': return literal("null");
        default: return number();
        }
    }
    ValidationResult literal(std::string_view expected)
    {
        if (input_.substr(pos_, expected.size()) != expected) return failure(ContractError::InvalidJson);
        pos_ += expected.size();
        return success();
    }
    ValidationResult number()
    {
        const std::size_t start = pos_;
        if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9') return failure(ContractError::InvalidInteger);
        if (input_[pos_] == '0') ++pos_;
        else while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        if (pos_ < input_.size() && (input_[pos_] == '.' || input_[pos_] == 'e' || input_[pos_] == 'E'
                                    || input_[pos_] == '+' || input_[pos_] == '-')) return failure(ContractError::InvalidInteger);
        std::uint64_t parsed = 0;
        const auto conversion = std::from_chars(input_.data() + start, input_.data() + pos_, parsed);
        if (conversion.ec != std::errc{} || parsed > kMaxJsonInteger) return failure(ContractError::InvalidInteger);
        return success();
    }
    ValidationResult string(std::string &decoded)
    {
        if (!take('"')) return failure(ContractError::InvalidJson);
        while (pos_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
            if (ch == '"') return success();
            if (ch < 0x20) return failure(ContractError::InvalidJson);
            if (ch != '\\') { decoded.push_back(static_cast<char>(ch)); continue; }
            if (pos_ >= input_.size()) return failure(ContractError::InvalidJson);
            const char escaped = input_[pos_++];
            switch (escaped) {
            case '"': case '\\': case '/': decoded.push_back(escaped); break;
            case 'b': decoded.push_back('\b'); break;
            case 'f': decoded.push_back('\f'); break;
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            case 'u': {
                if (pos_ + 4 > input_.size()) return failure(ContractError::InvalidJson);
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = input_[pos_++];
                    value <<= 4;
                    if (hex >= '0' && hex <= '9') value += static_cast<unsigned>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') value += static_cast<unsigned>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') value += static_cast<unsigned>(hex - 'A' + 10);
                    else return failure(ContractError::InvalidJson);
                }
                if (value >= 0xd800 && value <= 0xdfff) return failure(ContractError::InvalidJson);
                appendUtf8(decoded, value);
                break;
            }
            default: return failure(ContractError::InvalidJson);
            }
        }
        return failure(ContractError::InvalidJson);
    }
    ValidationResult object(int depth)
    {
        take('{'); skipSpace();
        if (take('}')) return success();
        std::set<std::string> keys;
        std::size_t items = 0;
        for (;;) {
            if (++items > 64) return failure(ContractError::LimitExceeded);
            std::string key;
            auto result = string(key);
            if (!result.ok) return result;
            if (!keys.insert(key).second) return failure(ContractError::DuplicateKey);
            skipSpace(); if (!take(':')) return failure(ContractError::InvalidJson);
            skipSpace(); result = value(depth + 1); if (!result.ok) return result;
            skipSpace(); if (take('}')) return success();
            if (!take(',')) return failure(ContractError::InvalidJson);
            skipSpace();
        }
    }
    ValidationResult array(int depth)
    {
        take('['); skipSpace();
        if (take(']')) return success();
        std::size_t items = 0;
        for (;;) {
            if (++items > 64) return failure(ContractError::LimitExceeded);
            auto result = value(depth + 1); if (!result.ok) return result;
            skipSpace(); if (take(']')) return success();
            if (!take(',')) return failure(ContractError::InvalidJson);
            skipSpace();
        }
    }
    std::string_view input_;
    std::size_t pos_{0};
};

bool hasExactFields(const QJsonObject &object, const std::set<QString> &required,
                    const std::set<QString> &optional = {})
{
    for (const auto &field : required) if (!object.contains(field)) return false;
    for (auto it = object.begin(); it != object.end(); ++it)
        if (required.find(it.key()) == required.end()
            && optional.find(it.key()) == optional.end()) return false;
    return true;
}

bool extractIdentity(const QJsonValue &value, std::string &kind, std::string &id)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    if (!hasExactFields(object, {"kind", "id"}) || !object["kind"].isString()
        || !object["id"].isString()) return false;
    kind = object["kind"].toString().toStdString();
    id = object["id"].toString().toStdString();
    if (kind != "device" && kind != "operator" && kind != "authority") return false;
    return validOpaque(id);
}

bool payloadFieldsValid(std::string_view type, const QJsonObject &payload)
{
    if (type == "signaling.offer" || type == "signaling.answer")
        return hasExactFields(payload, {"sdp"}) && payload["sdp"].isString()
            && payload["sdp"].toString().toUtf8().size() <= static_cast<int>(kMaxSdpBytes);
    if (type == "signaling.candidate")
        return hasExactFields(payload, {"candidate", "mid", "mLineIndex"})
            && payload["candidate"].isString() && payload["mid"].isString()
            && payload["mLineIndex"].isDouble()
            && payload["candidate"].toString().toUtf8().size() <= static_cast<int>(kMaxCandidateBytes)
            && payload["mid"].toString().toUtf8().size() <= static_cast<int>(kMaxMidBytes);
    if (type == "signaling.end_of_candidates") return payload.isEmpty();
    if (type == "message.ack") return hasExactFields(payload, {"result"}) && payload["result"].isString();
    if (type == "message.rejected") return hasExactFields(payload, {"code"}) && payload["code"].isString();
    if (type == "control.command") return hasExactFields(payload, {"command"}) && payload["command"].isString();
    if (type == "control.receipt" || type == "control.safety")
        return hasExactFields(payload, {"code"}) && payload["code"].isString();
    if (type == "session.reject") return hasExactFields(payload, {"reason"}) && payload["reason"].isString();
    if (type == "presence.online" || type == "presence.offline" || type == "capabilities"
        || type == "busy" || type == "heartbeat" || type == "telemetry.snapshot"
        || type == "session.request" || type == "session.accept" || type == "session.cancel")
        return payload.size() <= 64;
    return false;
}

std::string candidateKey(std::string_view candidate)
{
    std::string key(candidate);
    key.erase(std::unique(key.begin(), key.end(), [](char a, char b) {
        return a == ' ' && b == ' ';
    }), key.end());
    return key;
}

} // namespace

std::string_view errorCode(ContractError error) noexcept
{
    switch (error) {
    case ContractError::None: return "ok";
    case ContractError::InvalidUtf8: return "invalid_utf8";
    case ContractError::InvalidJson: return "invalid_json";
    case ContractError::DuplicateKey: return "duplicate_key";
    case ContractError::LimitExceeded: return "limit_exceeded";
    case ContractError::InvalidInteger: return "invalid_integer";
    case ContractError::UnknownField: return "unknown_field";
    case ContractError::MissingField: return "missing_field";
    case ContractError::UnknownMessageType: return "unknown_message_type";
    case ContractError::InvalidIdentity: return "invalid_identity";
    case ContractError::InvalidTopic: return "invalid_topic";
    case ContractError::UnauthorizedTopic: return "unauthorized_topic";
    case ContractError::Expired: return "expired";
    case ContractError::ClockSkew: return "clock_skew";
    case ContractError::Replay: return "replay";
    case ContractError::Tombstoned: return "tombstoned";
    case ContractError::AckTimeout: return "ack_timeout";
    case ContractError::InvalidTransition: return "invalid_transition";
    case ContractError::CandidateAfterEnd: return "candidate_after_eoc";
    case ContractError::DuplicateCandidate: return "duplicate_candidate";
    }
    return "invalid_contract_error";
}

std::optional<std::string> TopicCodec::encode(const TopicRoute &r)
{
    if ((!r.deviceId.empty() && !validOpaque(r.deviceId))
        || (!r.controlTargetId.empty() && !validOpaque(r.controlTargetId))
        || (!r.operatorId.empty() && !validOpaque(r.operatorId))
        || (!r.clientInstanceId.empty() && !validOpaque(r.clientInstanceId))) return std::nullopt;
    const std::string root(kTopicRoot);
    switch (r.kind) {
    case TopicKind::Presence: if (!r.deviceId.empty()) return root + "/presence/device/" + r.deviceId; break;
    case TopicKind::Capabilities: if (!r.deviceId.empty()) return root + "/capabilities/device/" + r.deviceId; break;
    case TopicKind::Busy: if (!r.deviceId.empty()) return root + "/busy/device/" + r.deviceId; break;
    case TopicKind::SignalToDevice:
        if (!r.deviceId.empty() && !r.operatorId.empty() && !r.clientInstanceId.empty())
            return root + "/signaling/to/device/" + r.deviceId + "/from/operator/" + r.operatorId + "/" + r.clientInstanceId;
        break;
    case TopicKind::SignalToOperator:
        if (!r.deviceId.empty() && !r.operatorId.empty() && !r.clientInstanceId.empty())
            return root + "/signaling/to/operator/" + r.operatorId + "/" + r.clientInstanceId + "/from/device/" + r.deviceId;
        break;
    case TopicKind::ControlCommand:
        if (!r.controlTargetId.empty() && !r.operatorId.empty() && !r.clientInstanceId.empty())
            return root + "/control/to/device/" + r.controlTargetId + "/from/operator/" + r.operatorId + "/" + r.clientInstanceId + "/command";
        break;
    case TopicKind::ControlReceipt: case TopicKind::ControlSafety:
        if (!r.controlTargetId.empty() && !r.operatorId.empty() && !r.clientInstanceId.empty())
            return root + "/control/to/operator/" + r.operatorId + "/" + r.clientInstanceId + "/from/device/" + r.controlTargetId
                + (r.kind == TopicKind::ControlReceipt ? "/receipt" : "/safety");
        break;
    case TopicKind::Heartbeat: if (!r.deviceId.empty()) return root + "/telemetry/device/" + r.deviceId + "/heartbeat"; break;
    case TopicKind::TelemetrySnapshot: if (!r.deviceId.empty()) return root + "/telemetry/device/" + r.deviceId + "/snapshot"; break;
    }
    return std::nullopt;
}

std::optional<TopicRoute> TopicCodec::decode(std::string_view topic)
{
    if (topic.find('#') != std::string_view::npos || topic.find('+') != std::string_view::npos) return std::nullopt;
    const auto p = split(topic);
    if (p.size() < 5 || p[0] != "rtmp-monitor" || p[1] != "v1") return std::nullopt;
    TopicRoute r;
    if (p.size() == 5 && p[3] == "device") {
        if (p[2] == "presence") r.kind = TopicKind::Presence;
        else if (p[2] == "capabilities") r.kind = TopicKind::Capabilities;
        else if (p[2] == "busy") r.kind = TopicKind::Busy;
        else return std::nullopt;
        r.deviceId = p[4];
    } else if (p.size() == 9 && p[2] == "signaling" && p[3] == "to" && p[4] == "device" && p[6] == "from" && p[7] == "operator") {
        r.kind = TopicKind::SignalToDevice; r.deviceId = p[5]; r.operatorId = p[8];
        return std::nullopt; // source-bound route requires the client instance segment
    } else if (p.size() == 10 && p[2] == "signaling" && p[3] == "to" && p[4] == "device" && p[6] == "from" && p[7] == "operator") {
        r.kind = TopicKind::SignalToDevice; r.deviceId = p[5]; r.operatorId = p[8]; r.clientInstanceId = p[9];
    } else if (p.size() == 10 && p[2] == "signaling" && p[3] == "to" && p[4] == "operator" && p[7] == "from" && p[8] == "device") {
        r.kind = TopicKind::SignalToOperator; r.operatorId = p[5]; r.clientInstanceId = p[6]; r.deviceId = p[9];
    } else if (p.size() == 11 && p[2] == "control" && p[3] == "to" && p[4] == "device" && p[6] == "from" && p[7] == "operator" && p[10] == "command") {
        r.kind = TopicKind::ControlCommand; r.controlTargetId = p[5]; r.operatorId = p[8]; r.clientInstanceId = p[9];
    } else if (p.size() == 11 && p[2] == "control" && p[3] == "to" && p[4] == "operator" && p[7] == "from" && p[8] == "device" && (p[10] == "receipt" || p[10] == "safety")) {
        r.kind = p[10] == "receipt" ? TopicKind::ControlReceipt : TopicKind::ControlSafety;
        r.operatorId = p[5]; r.clientInstanceId = p[6]; r.controlTargetId = p[9];
    } else if (p.size() == 6 && p[2] == "telemetry" && p[3] == "device" && (p[5] == "heartbeat" || p[5] == "snapshot")) {
        r.kind = p[5] == "heartbeat" ? TopicKind::Heartbeat : TopicKind::TelemetrySnapshot; r.deviceId = p[4];
    } else return std::nullopt;
    const auto encoded = encode(r);
    if (!encoded || *encoded != topic) return std::nullopt;
    return r;
}

ValidationResult AclPolicy::authorize(const AclContext &c, Access a, std::string_view topic)
{
    const auto route = TopicCodec::decode(topic);
    if (!route) return failure(ContractError::InvalidTopic);
    bool allowed = false;
    if (c.devicePrincipal) {
        if (route->kind == TopicKind::Presence || route->kind == TopicKind::Capabilities || route->kind == TopicKind::Busy
            || route->kind == TopicKind::Heartbeat || route->kind == TopicKind::TelemetrySnapshot)
            allowed = a == Access::Publish && route->deviceId == c.deviceId;
        else if (route->kind == TopicKind::SignalToDevice)
            allowed = a == Access::Subscribe && route->deviceId == c.deviceId && route->operatorId == c.operatorId && route->clientInstanceId == c.clientInstanceId;
        else if (route->kind == TopicKind::SignalToOperator)
            allowed = a == Access::Publish && route->deviceId == c.deviceId && route->operatorId == c.operatorId && route->clientInstanceId == c.clientInstanceId;
        else if (route->kind == TopicKind::ControlCommand)
            allowed = c.scope == ProvisioningScope::Control && a == Access::Subscribe
                && route->controlTargetId == c.controlTargetId && route->operatorId == c.operatorId
                && route->clientInstanceId == c.clientInstanceId;
        else if (route->kind == TopicKind::ControlReceipt || route->kind == TopicKind::ControlSafety)
            allowed = c.scope == ProvisioningScope::Control && a == Access::Publish
                && route->controlTargetId == c.controlTargetId && route->operatorId == c.operatorId
                && route->clientInstanceId == c.clientInstanceId;
    } else {
        if (route->kind == TopicKind::Presence || route->kind == TopicKind::Capabilities || route->kind == TopicKind::Busy
            || route->kind == TopicKind::Heartbeat || route->kind == TopicKind::TelemetrySnapshot)
            allowed = a == Access::Subscribe && route->deviceId == c.deviceId;
        else if (route->kind == TopicKind::SignalToDevice)
            allowed = a == Access::Publish && route->deviceId == c.deviceId && route->operatorId == c.operatorId && route->clientInstanceId == c.clientInstanceId;
        else if (route->kind == TopicKind::SignalToOperator)
            allowed = a == Access::Subscribe && route->deviceId == c.deviceId && route->operatorId == c.operatorId && route->clientInstanceId == c.clientInstanceId;
        else if (route->kind == TopicKind::ControlCommand)
            allowed = c.scope == ProvisioningScope::Control && a == Access::Publish
                && route->controlTargetId == c.controlTargetId && route->operatorId == c.operatorId
                && route->clientInstanceId == c.clientInstanceId;
        else if (route->kind == TopicKind::ControlReceipt || route->kind == TopicKind::ControlSafety)
            allowed = c.scope == ProvisioningScope::Control && a == Access::Subscribe
                && route->controlTargetId == c.controlTargetId && route->operatorId == c.operatorId
                && route->clientInstanceId == c.clientInstanceId;
    }
    return allowed ? success() : failure(ContractError::UnauthorizedTopic);
}

std::optional<MessagePolicy> policyFor(std::string_view type)
{
    if (type == "message.ack" || type == "message.rejected") return MessagePolicy{1, false, 120000, 4096, false};
    if (type == "signaling.offer" || type == "signaling.answer") return MessagePolicy{1, false, 120000, kMaxSdpBytes, true};
    if (type == "signaling.candidate") return MessagePolicy{1, false, 120000, kMaxCandidateBytes, true};
    if (type == "signaling.end_of_candidates") return MessagePolicy{1, false, 120000, 1024, true};
    if (type == "session.request" || type == "session.accept" || type == "session.reject" || type == "session.cancel"
        || type == "presence.online" || type == "presence.offline" || type == "capabilities" || type == "busy"
        || type == "heartbeat" || type == "telemetry.snapshot" || type == "control.command"
        || type == "control.receipt" || type == "control.safety") return MessagePolicy{};
    return std::nullopt;
}

EnvelopeDecodeResult EnvelopeCodec::decode(std::string_view json)
{
    if (json.size() > kMaxEnvelopeBytes || json.find('\0') != std::string_view::npos)
        return {failure(ContractError::LimitExceeded), std::nullopt};
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    if (decoder.hasError()) return {failure(ContractError::InvalidUtf8), std::nullopt};
    const auto lexical = JsonScanner(json).scan();
    if (!lexical.ok) return {lexical, std::nullopt};
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {failure(ContractError::InvalidJson), std::nullopt};
    const auto object = document.object();
    const std::set<QString> required{"schemaVersion", "messageId", "messageType", "sentAtUtc", "ttlMs", "sourceIdentity", "targetIdentity", "sequence", "payload"};
    const std::set<QString> optional{"sourceClientInstanceId", "sessionId", "attemptId", "correlationId", "sessionNonce"};
    for (const auto &field : required) if (!object.contains(field)) return {failure(ContractError::MissingField), std::nullopt};
    if (!hasExactFields(object, required, optional)) return {failure(ContractError::UnknownField), std::nullopt};
    if (!object["schemaVersion"].isDouble() || object["schemaVersion"].toInt() != 1
        || !object["messageId"].isString() || !object["messageType"].isString()
        || !object["sentAtUtc"].isString() || !object["ttlMs"].isDouble()
        || !object["sequence"].isDouble() || !object["payload"].isObject())
        return {failure(ContractError::InvalidJson), std::nullopt};
    Envelope e;
    e.schemaVersion = 1;
    e.messageId = object["messageId"].toString().toStdString();
    e.messageType = object["messageType"].toString().toStdString();
    e.sentAtUtc = object["sentAtUtc"].toString().toStdString();
    e.ttlMs = static_cast<std::uint64_t>(object["ttlMs"].toDouble());
    e.sequence = static_cast<std::uint64_t>(object["sequence"].toDouble());
    if (!validUuid(e.messageId)) return {failure(ContractError::InvalidIdentity), std::nullopt};
    const auto policy = policyFor(e.messageType);
    if (!policy) return {failure(ContractError::UnknownMessageType), std::nullopt};
    if (e.ttlMs == 0 || e.ttlMs > policy->maximumTtlMs) return {failure(ContractError::LimitExceeded), std::nullopt};
    const auto sent = QDateTime::fromString(QString::fromStdString(e.sentAtUtc), Qt::ISODateWithMs);
    if (!sent.isValid() || e.sentAtUtc.empty() || e.sentAtUtc.back() != 'Z')
        return {failure(ContractError::InvalidJson), std::nullopt};
    if (!extractIdentity(object["sourceIdentity"], e.sourceKind, e.sourceId)
        || !extractIdentity(object["targetIdentity"], e.targetKind, e.targetId))
        return {failure(ContractError::InvalidIdentity), std::nullopt};
    auto optionalString = [&](const char *name, std::string &target) {
        if (!object.contains(name)) return true;
        if (!object[name].isString()) return false;
        target = object[name].toString().toStdString(); return true;
    };
    if (!optionalString("sourceClientInstanceId", e.sourceClientInstanceId)
        || !optionalString("sessionId", e.sessionId) || !optionalString("attemptId", e.attemptId)
        || !optionalString("correlationId", e.correlationId) || !optionalString("sessionNonce", e.sessionNonce))
        return {failure(ContractError::InvalidJson), std::nullopt};
    if ((!e.sourceClientInstanceId.empty() && !validOpaque(e.sourceClientInstanceId))
        || (!e.sessionId.empty() && !validUuid(e.sessionId)) || (!e.attemptId.empty() && !validUuid(e.attemptId))
        || (!e.correlationId.empty() && !validUuid(e.correlationId))
        || (!e.sessionNonce.empty() && !validOpaque(e.sessionNonce)))
        return {failure(ContractError::InvalidIdentity), std::nullopt};
    const auto payload = object["payload"].toObject();
    if (!payloadFieldsValid(e.messageType, payload)) return {failure(ContractError::UnknownField), std::nullopt};
    const auto payloadBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (static_cast<std::size_t>(payloadBytes.size()) > policy->maximumPayloadBytes)
        return {failure(ContractError::LimitExceeded), std::nullopt};
    e.payloadJson.assign(payloadBytes.constData(), static_cast<std::size_t>(payloadBytes.size()));
    return {success(), std::move(e)};
}

std::optional<std::string> EnvelopeCodec::encode(const Envelope &e)
{
    QJsonParseError payloadError;
    const auto payload = QJsonDocument::fromJson(QByteArray::fromStdString(e.payloadJson), &payloadError);
    if (payloadError.error != QJsonParseError::NoError || !payload.isObject()) return std::nullopt;
    QJsonObject source{{"kind", QString::fromStdString(e.sourceKind)}, {"id", QString::fromStdString(e.sourceId)}};
    QJsonObject target{{"kind", QString::fromStdString(e.targetKind)}, {"id", QString::fromStdString(e.targetId)}};
    QJsonObject object{{"schemaVersion", e.schemaVersion}, {"messageId", QString::fromStdString(e.messageId)},
        {"messageType", QString::fromStdString(e.messageType)}, {"sentAtUtc", QString::fromStdString(e.sentAtUtc)},
        {"ttlMs", static_cast<double>(e.ttlMs)}, {"sourceIdentity", source}, {"targetIdentity", target},
        {"sequence", static_cast<double>(e.sequence)}, {"payload", payload.object()}};
    auto add = [&](const char *name, const std::string &value) { if (!value.empty()) object[name] = QString::fromStdString(value); };
    add("sourceClientInstanceId", e.sourceClientInstanceId); add("sessionId", e.sessionId); add("attemptId", e.attemptId);
    add("correlationId", e.correlationId); add("sessionNonce", e.sessionNonce);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    std::string encoded(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    if (!decode(encoded).validation.ok) return std::nullopt;
    return encoded;
}

ValidationResult TemporalPolicy::validate(std::int64_t sent, std::uint64_t ttl, std::int64_t now, std::int64_t skew)
{
    if (sent > now + skew) return failure(ContractError::ClockSkew);
    if (sent < now - skew || ttl == 0 || (sent <= now && static_cast<std::uint64_t>(now - sent) > ttl))
        return failure(ContractError::Expired);
    return success();
}

ReplayGuard::ReplayGuard(std::size_t maxEntries, std::size_t maxBytes, std::int64_t retentionMs)
    : maxEntries_(maxEntries), maxBytes_(maxBytes), retentionMs_(retentionMs) {}
void ReplayGuard::prune(std::int64_t now)
{
    while (!entries_.empty() && (entries_.front().expiresAt <= now || entries_.size() > maxEntries_ || bytes_ > maxBytes_)) {
        bytes_ -= entries_.front().bytes; keys_.erase(entries_.front().key); entries_.pop_front();
    }
}
ValidationResult ReplayGuard::remember(std::string_view principal, std::string_view messageId, std::size_t resultBytes, std::int64_t now)
{
    prune(now); const std::string key = std::string(principal) + "\n" + std::string(messageId);
    if (keys_.find(key) != keys_.end()) return failure(ContractError::Replay);
    if (resultBytes > maxBytes_) return failure(ContractError::LimitExceeded);
    entries_.push_back({key, resultBytes, now + retentionMs_}); keys_.insert(key); bytes_ += resultBytes; prune(now);
    return success();
}

TombstoneStore::TombstoneStore(std::int64_t retentionMs) : retentionMs_(retentionMs) {}
void TombstoneStore::close(std::string session, std::string attempt, std::string nonce, std::int64_t now)
{ entries_.push_back({session + "\n" + attempt + "\n" + nonce, now + retentionMs_}); }
ValidationResult TombstoneStore::accept(std::string_view session, std::string_view attempt, std::string_view nonce, std::int64_t now)
{
    while (!entries_.empty() && entries_.front().expiresAt <= now) entries_.pop_front();
    const std::string key = std::string(session) + "\n" + std::string(attempt) + "\n" + std::string(nonce);
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry &e) { return e.key == key; })
        ? failure(ContractError::Tombstoned) : success();
}

void AckTracker::start(std::string id, std::int64_t now) { states_[std::move(id)] = {now + 3000, false}; }
AckAction AckTracker::acknowledge(std::string_view id)
{ auto it = states_.find(std::string(id)); if (it == states_.end()) return AckAction::NotTracked; states_.erase(it); return AckAction::Complete; }
AckAction AckTracker::poll(std::string_view id, std::int64_t now)
{
    auto it = states_.find(std::string(id)); if (it == states_.end()) return AckAction::NotTracked;
    if (now < it->second.deadline) return AckAction::Waiting;
    if (!it->second.retried) { it->second.retried = true; it->second.deadline = now + 3000; return AckAction::Retry; }
    states_.erase(it); return AckAction::TimedOut;
}

ValidationResult SessionTransitionPolicy::validate(SessionState from, SessionState to)
{
    const bool ok = (from == SessionState::Idle && to == SessionState::Requested)
        || (from == SessionState::Requested && (to == SessionState::Accepted || to == SessionState::Rejected || to == SessionState::Closing))
        || (from == SessionState::Accepted && (to == SessionState::Negotiating || to == SessionState::Closing))
        || (from == SessionState::Negotiating && (to == SessionState::Connected || to == SessionState::Failed || to == SessionState::Closing))
        || (from == SessionState::Connected && (to == SessionState::Closing || to == SessionState::Failed))
        || (from == SessionState::Closing && to == SessionState::Closed);
    return ok ? success() : failure(ContractError::InvalidTransition);
}
ValidationResult DeviceAgentTransitionPolicy::validate(DeviceAgentState from, DeviceAgentState to)
{
    const bool ok = (from == DeviceAgentState::Offline && to == DeviceAgentState::Online)
        || (from == DeviceAgentState::Online && (to == DeviceAgentState::Reserved || to == DeviceAgentState::Offline || to == DeviceAgentState::Faulted))
        || (from == DeviceAgentState::Reserved && (to == DeviceAgentState::Negotiating || to == DeviceAgentState::Closing))
        || (from == DeviceAgentState::Negotiating && (to == DeviceAgentState::Streaming || to == DeviceAgentState::Closing || to == DeviceAgentState::Faulted))
        || (from == DeviceAgentState::Streaming && (to == DeviceAgentState::Closing || to == DeviceAgentState::Faulted))
        || (from == DeviceAgentState::Closing && to == DeviceAgentState::Online)
        || (from == DeviceAgentState::Faulted && (to == DeviceAgentState::Closing || to == DeviceAgentState::Offline));
    return ok ? success() : failure(ContractError::InvalidTransition);
}

ValidationResult CandidatePolicy::accept(const CandidateInput &input)
{
    if (ended_) return failure(ContractError::CandidateAfterEnd);
    if (input.endOfCandidates) { ended_ = true; return success(); }
    if (input.candidate.empty() || input.candidate.size() > kMaxCandidateBytes || input.mid.size() > kMaxMidBytes
        || count_ >= 64 || bytes_ + input.candidate.size() > 64U * 1024U) return failure(ContractError::LimitExceeded);
    const auto key = candidateKey(input.candidate);
    if (!semanticKeys_.insert(key).second) return failure(ContractError::DuplicateCandidate);
    ++count_; bytes_ += input.candidate.size(); return success();
}

} // namespace rtmp::p2p
