#include "signaling_session/DirectSessionCore.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace rtmp::p2p;

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

class FakeChannel final : public ISignalingChannel
{
public:
    void setStateHandler(StateHandler handler) override
    { stateHandler = std::move(handler); }
    void setMessageHandler(MessageHandler handler) override
    { messageHandler = std::move(handler); }
    bool start(const std::vector<std::string> &topics) override
    {
        subscriptions = topics;
        if (stateHandler) stateHandler(SignalingChannelState::Ready, {});
        return true;
    }
    void stop() override
    { if (stateHandler) stateHandler(SignalingChannelState::Stopped, {}); }
    bool publish(const SignalingPublish &message) override
    {
        published.push_back(message);
        if (peer && peer->messageHandler) {
            peer->messageHandler({message.topic, message.payload, message.qos,
                false, message.retained, message.expirySeconds, 1, now});
        }
        return true;
    }
    void inject(const SignalingPublish &message, std::string overrideTopic = {})
    {
        if (!messageHandler) return;
        messageHandler({overrideTopic.empty() ? message.topic : std::move(overrideTopic),
            message.payload, message.qos, true, message.retained,
            message.expirySeconds, 1, now});
    }

    FakeChannel *peer{nullptr};
    std::int64_t now{1704067200000};
    StateHandler stateHandler;
    MessageHandler messageHandler;
    std::vector<std::string> subscriptions;
    std::vector<SignalingPublish> published;
};

struct IdSequence final {
    int value{1};
    std::string next()
    {
        const char digit = static_cast<char>('0' + (value++ % 10));
        return std::string("00000000-0000-4000-8000-00000000000") + digit;
    }
};
}

int main()
{
    constexpr std::int64_t now = 1704067200000;
    FakeChannel operatorChannel;
    FakeChannel deviceChannel;
    operatorChannel.peer = &deviceChannel;
    deviceChannel.peer = &operatorChannel;
    operatorChannel.now = now;
    deviceChannel.now = now;
    IdSequence ids;
    const DirectRouteIdentity identity{"device-1", "operator-1", "desktop-1"};
    DirectOperatorCore operatorCore(operatorChannel, identity, [now] { return now; },
        [&ids] { return ids.next(); });
    DirectDeviceCore deviceCore(deviceChannel, identity, [now] { return now; },
        [&ids] { return ids.next(); });
    int startActions = 0;
    int stopActions = 0;
    deviceCore.setActionHandler([&](DirectAction action) {
        if (action == DirectAction::StartStream) ++startActions;
        else ++stopActions;
    });

    require(deviceCore.start(), "device start");
    require(operatorCore.start(), "operator start");
    require(deviceChannel.subscriptions.size() == 1, "device exact subscription");
    require(operatorChannel.subscriptions.size() == 2, "operator exact subscriptions");
    require(operatorCore.requestStartStream(), "session request");
    require(startActions == 1, "start action exactly once");
    require(operatorCore.snapshot().sessionState == SessionState::Connected,
            "operator reaches connected");
    require(deviceCore.snapshot().deviceState == DeviceAgentState::Streaming,
            "device reaches streaming");

    const SignalingPublish originalRequest = operatorChannel.published.front();
    deviceChannel.inject(originalRequest);
    require(startActions == 1, "duplicate request has no second side effect");
    require(deviceCore.snapshot().duplicates == 1, "duplicate counted");
    require(operatorCore.snapshot().duplicates == 3, "duplicate replies counted");

    const auto rejectedBefore = deviceCore.snapshot().rejected;
    deviceChannel.inject(originalRequest, "rtmp-monitor/v1/signaling/to/device/device-2/from/operator/operator-1/desktop-1");
    require(deviceCore.snapshot().rejected == rejectedBefore + 1,
            "wrong device route rejected");

    require(operatorCore.requestStopStream(), "session cancel");
    require(stopActions == 1, "stop action exactly once");
    require(operatorCore.snapshot().sessionState == SessionState::Closed,
            "operator reaches closed");
    require(deviceCore.snapshot().deviceState == DeviceAgentState::Online,
            "device returns online");

    FakeChannel timeoutChannel;
    IdSequence timeoutIds;
    std::int64_t timeoutNow = now;
    DirectOperatorCore timeoutCore(timeoutChannel, identity,
        [&timeoutNow] { return timeoutNow; },
        [&timeoutIds] { return timeoutIds.next(); });
    require(timeoutCore.start(), "timeout core start");
    require(timeoutCore.requestStartStream(), "timeout request");
    timeoutNow += 3001;
    timeoutCore.poll();
    require(timeoutChannel.published.size() == 2, "one ack retry");
    timeoutNow += 3001;
    timeoutCore.poll();
    require(timeoutCore.snapshot().lastError == "ack_timeout", "ack timeout");

    std::cout << "direct_session_core_passed\n";
    return 0;
}
