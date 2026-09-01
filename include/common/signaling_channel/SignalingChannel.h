#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rtmp::p2p {

enum class SignalingChannelState {
    Disabled,
    Connecting,
    Subscribing,
    Ready,
    Reconnecting,
    Stopped,
    Error
};

struct SignalingFrame final {
    std::string topic;
    std::string payload;
    int qos{1};
    bool duplicate{false};
    bool retained{false};
    std::uint32_t expirySeconds{0};
    std::uint64_t connectionEpoch{0};
    std::int64_t receivedAtUnixMs{0};
};

struct SignalingPublish final {
    std::string topic;
    std::string payload;
    int qos{1};
    bool retained{false};
    std::uint32_t expirySeconds{10};
};

/** Broker-neutral signaling seam. Implementations own their connection. */
class ISignalingChannel
{
public:
    using StateHandler =
        std::function<void(SignalingChannelState, const std::string &)>;
    using MessageHandler = std::function<void(const SignalingFrame &)>;

    virtual ~ISignalingChannel() = default;
    virtual void setStateHandler(StateHandler handler) = 0;
    virtual void setMessageHandler(MessageHandler handler) = 0;
    virtual bool start(const std::vector<std::string> &exactSubscriptions) = 0;
    virtual void stop() = 0;
    virtual bool publish(const SignalingPublish &message) = 0;
};

} // namespace rtmp::p2p
