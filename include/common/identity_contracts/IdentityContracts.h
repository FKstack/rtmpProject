#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rtmp::p2p {

template <typename Tag>
class OpaqueId final {
public:
    static std::optional<OpaqueId> parse(std::string_view value)
    {
        if (value.empty() || value.size() > 128) {
            return std::nullopt;
        }
        for (const unsigned char ch : value) {
            const bool allowed = (ch >= 'A' && ch <= 'Z')
                || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
                || ch == '.' || ch == '_' || ch == '-';
            if (!allowed) {
                return std::nullopt;
            }
        }
        return OpaqueId(std::string(value));
    }

    const std::string &value() const noexcept { return value_; }
    friend bool operator==(const OpaqueId &left, const OpaqueId &right)
    { return left.value_ == right.value_; }

private:
    explicit OpaqueId(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

template <typename Tag>
class UuidV4Id final {
public:
    static std::optional<UuidV4Id> parse(std::string_view value)
    {
        if (value.size() != 36 || value[8] != '-' || value[13] != '-'
            || value[18] != '-' || value[23] != '-' || value[14] != '4') {
            return std::nullopt;
        }
        if (value[19] != '8' && value[19] != '9' && value[19] != 'a'
            && value[19] != 'b') {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (index == 8 || index == 13 || index == 18 || index == 23) {
                continue;
            }
            const char ch = value[index];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
                return std::nullopt;
            }
        }
        return UuidV4Id(std::string(value));
    }

    const std::string &value() const noexcept { return value_; }
    friend bool operator==(const UuidV4Id &left, const UuidV4Id &right)
    { return left.value_ == right.value_; }

private:
    explicit UuidV4Id(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

struct DeviceIdTag;
struct UserIdTag;
struct ClientInstanceIdTag;
struct AuthorityIdTag;
struct MqttControlTargetIdTag;
struct MessageIdTag;
struct SessionIdTag;
struct AttemptIdTag;
struct ControlLeaseIdTag;

using DeviceId = OpaqueId<DeviceIdTag>;
using UserId = OpaqueId<UserIdTag>;
using ClientInstanceId = OpaqueId<ClientInstanceIdTag>;
using AuthorityId = OpaqueId<AuthorityIdTag>;
using MqttControlTargetId = OpaqueId<MqttControlTargetIdTag>;
using MessageId = UuidV4Id<MessageIdTag>;
using SessionId = UuidV4Id<SessionIdTag>;
using AttemptId = UuidV4Id<AttemptIdTag>;
using ControlLeaseId = UuidV4Id<ControlLeaseIdTag>;

enum class MqttPlane { Signal, Control };

class MqttClientIdCodec final {
public:
    static std::string device(const DeviceId &deviceId, MqttPlane plane);
    static std::string operatorClient(const UserId &userId,
                                      const ClientInstanceId &instanceId,
                                      MqttPlane plane);
};

} // namespace rtmp::p2p
