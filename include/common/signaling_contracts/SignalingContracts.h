#pragma once

#include "identity_contracts/IdentityContracts.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rtmp::p2p {

inline constexpr std::string_view kTopicRoot = "rtmp-monitor/v1";
inline constexpr std::size_t kMaxEnvelopeBytes = 256U * 1024U;
inline constexpr std::size_t kMaxSdpBytes = 192U * 1024U;
inline constexpr std::size_t kMaxCandidateBytes = 4U * 1024U;
inline constexpr std::size_t kMaxMidBytes = 64U;
inline constexpr std::uint64_t kMaxJsonInteger = 9007199254740991ULL;

enum class ContractError {
    None,
    InvalidUtf8,
    InvalidJson,
    DuplicateKey,
    LimitExceeded,
    InvalidInteger,
    UnknownField,
    MissingField,
    UnknownMessageType,
    InvalidIdentity,
    InvalidTopic,
    UnauthorizedTopic,
    Expired,
    ClockSkew,
    Replay,
    Tombstoned,
    AckTimeout,
    InvalidTransition,
    CandidateAfterEnd,
    DuplicateCandidate
};

std::string_view errorCode(ContractError error) noexcept;

struct ValidationResult final {
    bool ok{false};
    ContractError error{ContractError::None};
};

enum class TopicKind {
    Presence,
    Capabilities,
    Busy,
    SignalToDevice,
    SignalToOperator,
    ControlCommand,
    ControlReceipt,
    ControlSafety,
    Heartbeat,
    TelemetrySnapshot
};

struct TopicRoute final {
    TopicKind kind{TopicKind::Presence};
    std::string deviceId;
    std::string controlTargetId;
    std::string operatorId;
    std::string clientInstanceId;
};

class TopicCodec final {
public:
    static std::optional<std::string> encode(const TopicRoute &route);
    static std::optional<TopicRoute> decode(std::string_view topic);
};

enum class Access { Publish, Subscribe };
enum class ProvisioningScope { View, Control };

struct AclContext final {
    bool devicePrincipal{false};
    std::string deviceId;
    std::string controlTargetId;
    std::string operatorId;
    std::string clientInstanceId;
    ProvisioningScope scope{ProvisioningScope::View};
};

class AclPolicy final {
public:
    static ValidationResult authorize(const AclContext &context,
                                      Access access,
                                      std::string_view topic);
};

struct MessagePolicy final {
    int qos{1};
    bool retained{false};
    std::uint32_t messageExpirySeconds{10};
    std::uint64_t maximumTtlMs{120000};
    std::size_t maximumPayloadBytes{kMaxEnvelopeBytes};
    bool requiresAck{true};
};

std::optional<MessagePolicy> policyFor(std::string_view messageType);

struct Envelope final {
    int schemaVersion{1};
    std::string messageId;
    std::string messageType;
    std::string sentAtUtc;
    std::uint64_t ttlMs{0};
    std::string sourceKind;
    std::string sourceId;
    std::string sourceClientInstanceId;
    std::string targetKind;
    std::string targetId;
    std::string sessionId;
    std::string attemptId;
    std::string correlationId;
    std::uint64_t sequence{0};
    std::string sessionNonce;
    std::string payloadJson;
};

struct EnvelopeDecodeResult final {
    ValidationResult validation;
    std::optional<Envelope> envelope;
};

class EnvelopeCodec final {
public:
    static EnvelopeDecodeResult decode(std::string_view json);
    static std::optional<std::string> encode(const Envelope &envelope);
};

class TemporalPolicy final {
public:
    static ValidationResult validate(std::int64_t sentAtUnixMs,
                                     std::uint64_t ttlMs,
                                     std::int64_t nowUnixMs,
                                     std::int64_t maximumSkewMs = 120000);
};

class ReplayGuard final {
public:
    explicit ReplayGuard(std::size_t maxEntries = 4096,
                         std::size_t maxBytes = 8U * 1024U * 1024U,
                         std::int64_t retentionMs = 600000);
    ValidationResult remember(std::string_view authenticatedPrincipal,
                              std::string_view messageId,
                              std::size_t resultBytes,
                              std::int64_t nowUnixMs);

private:
    struct Entry { std::string key; std::size_t bytes; std::int64_t expiresAt; };
    void prune(std::int64_t nowUnixMs);
    std::size_t maxEntries_;
    std::size_t maxBytes_;
    std::int64_t retentionMs_;
    std::size_t bytes_{0};
    std::deque<Entry> entries_;
    std::unordered_set<std::string> keys_;
};

class TombstoneStore final {
public:
    explicit TombstoneStore(std::int64_t retentionMs = 600000);
    void close(std::string sessionId, std::string attemptId,
               std::string nonce, std::int64_t nowUnixMs);
    ValidationResult accept(std::string_view sessionId,
                            std::string_view attemptId,
                            std::string_view nonce,
                            std::int64_t nowUnixMs);
private:
    struct Entry { std::string key; std::int64_t expiresAt; };
    std::int64_t retentionMs_;
    std::deque<Entry> entries_;
};

enum class AckAction { Waiting, Retry, TimedOut, Complete, NotTracked };
class AckTracker final {
public:
    void start(std::string messageId, std::int64_t nowUnixMs);
    AckAction acknowledge(std::string_view messageId);
    AckAction poll(std::string_view messageId, std::int64_t nowUnixMs);
private:
    struct State { std::int64_t deadline; bool retried; };
    std::unordered_map<std::string, State> states_;
};

enum class SessionState { Idle, Requested, Accepted, Negotiating, Connected, Closing, Closed, Rejected, Failed };
enum class DeviceAgentState { Offline, Online, Reserved, Negotiating, Streaming, Closing, Faulted };
class SessionTransitionPolicy final {
public:
    static ValidationResult validate(SessionState from, SessionState to);
};
class DeviceAgentTransitionPolicy final {
public:
    static ValidationResult validate(DeviceAgentState from, DeviceAgentState to);
};

struct CandidateInput final {
    std::string candidate;
    std::string mid;
    bool endOfCandidates{false};
};
class CandidatePolicy final {
public:
    ValidationResult accept(const CandidateInput &input);
private:
    bool ended_{false};
    std::size_t count_{0};
    std::size_t bytes_{0};
    std::unordered_set<std::string> semanticKeys_;
};

} // namespace rtmp::p2p
