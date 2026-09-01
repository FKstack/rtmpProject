#pragma once

#include "signaling_channel/SignalingChannel.h"
#include "signaling_contracts/SignalingContracts.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtmp::p2p {

struct DirectRouteIdentity final {
    std::string deviceId;
    std::string operatorId;
    std::string clientInstanceId;
};

enum class DirectAction { StartStream, StopStream };

struct DirectCoreSnapshot final {
    SessionState sessionState{SessionState::Idle};
    DeviceAgentState deviceState{DeviceAgentState::Offline};
    std::uint64_t received{0};
    std::uint64_t published{0};
    std::uint64_t rejected{0};
    std::uint64_t duplicates{0};
    std::uint64_t actions{0};
    std::string lastError;
};

using DirectClock = std::function<std::int64_t()>;
using DirectIdFactory = std::function<std::string()>;

class DirectOperatorCore final
{
public:
    DirectOperatorCore(ISignalingChannel &channel, DirectRouteIdentity identity,
                       DirectClock clock = {}, DirectIdFactory ids = {});
    bool start();
    void stop();
    bool requestStartStream();
    bool requestStopStream();
    void poll();
    [[nodiscard]] DirectCoreSnapshot snapshot() const;
    void setChangedHandler(std::function<void(const DirectCoreSnapshot &)> handler);

private:
    bool publishSessionMessage(const std::string &type,
                               const std::string &payload,
                               bool trackAck);
    void receive(const SignalingFrame &frame);
    void updateState(SessionState next);
    void changed();

    ISignalingChannel &channel_;
    DirectRouteIdentity identity_;
    DirectClock clock_;
    DirectIdFactory ids_;
    DirectCoreSnapshot snapshot_;
    ReplayGuard replayGuard_;
    AckTracker ackTracker_;
    std::string sessionId_;
    std::string attemptId_;
    std::string nonce_;
    std::string pendingMessageId_;
    SignalingPublish pendingPublish_;
    std::function<void(const DirectCoreSnapshot &)> changedHandler_;
};

class DirectDeviceCore final
{
public:
    DirectDeviceCore(ISignalingChannel &channel, DirectRouteIdentity identity,
                     DirectClock clock = {}, DirectIdFactory ids = {});
    bool start();
    void stop();
    [[nodiscard]] DirectCoreSnapshot snapshot() const;
    void setActionHandler(std::function<void(DirectAction)> handler);
    void setChangedHandler(std::function<void(const DirectCoreSnapshot &)> handler);

private:
    void receive(const SignalingFrame &frame);
    bool publishReply(const Envelope &request, const std::string &type,
                      const std::string &payload);
    bool replayCached(const std::string &messageId);
    void changed();

    ISignalingChannel &channel_;
    DirectRouteIdentity identity_;
    DirectClock clock_;
    DirectIdFactory ids_;
    DirectCoreSnapshot snapshot_;
    ReplayGuard replayGuard_;
    std::unordered_map<std::string, std::vector<SignalingPublish>> cache_;
    std::function<void(DirectAction)> actionHandler_;
    std::function<void(const DirectCoreSnapshot &)> changedHandler_;
};

} // namespace rtmp::p2p
