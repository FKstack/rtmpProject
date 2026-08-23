#include "webrtc_transport/WebRtcEndpointSession.h"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace rtmp_monitor::webrtc_transport {
namespace {

constexpr std::size_t kMaximumAccessUnitBytes = 4U * 1024U * 1024U;
constexpr std::size_t kQueueCapacity = 2;
constexpr std::uint8_t kPayloadType = 102;
constexpr std::uint32_t kSsrc = 0x52544d50U;
constexpr std::size_t kMaximumFragmentBytes = 1200;

std::string candidateTypeName(rtc::Candidate::Type type)
{
    switch (type) {
    case rtc::Candidate::Type::Host: return "host";
    case rtc::Candidate::Type::ServerReflexive: return "srflx";
    case rtc::Candidate::Type::PeerReflexive: return "prflx";
    case rtc::Candidate::Type::Relayed: return "relay";
    case rtc::Candidate::Type::Unknown: break;
    }
    return "unknown";
}

std::vector<std::string> candidateTypes(const rtc::Description &description)
{
    std::vector<std::string> result;
    for (const rtc::Candidate &candidate : description.candidates()) {
        const std::string type = candidateTypeName(candidate.type());
        if (std::find(result.begin(), result.end(), type) == result.end()) {
            result.push_back(type);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct SharedState
{
    std::mutex mutex;
    std::condition_variable changed;
    EndpointState endpointState = EndpointState::New;
    bool closing = false;
    bool gatheringComplete = false;
    bool connected = false;
    bool failed = false;
    bool waitingForKeyframe = false;
    std::uint64_t generation = 1;
    std::uint64_t acceptedAccessUnits = 0;
    std::uint64_t droppedAccessUnits = 0;
    std::uint64_t sentAccessUnits = 0;
    std::uint64_t receivedRtpPackets = 0;
    std::uint64_t sendFailures = 0;
    std::deque<H264AccessUnit> queue;
    std::shared_ptr<rtc::PeerConnection> connection;
    std::shared_ptr<rtc::Track> localTrack;
    std::shared_ptr<rtc::Track> remoteTrack;
    std::vector<std::string> localCandidateTypes;
};

H264SubmitResult enqueueAccessUnit(
    const std::weak_ptr<SharedState> &weakState,
    std::uint64_t generation,
    H264AccessUnit accessUnit
)
{
    const auto state = weakState.lock();
    if (!state) return H264SubmitResult::Closed;
    if (!isValidH264AccessUnit(accessUnit, kMaximumAccessUnitBytes)) {
        return H264SubmitResult::InvalidAccessUnit;
    }

    const std::lock_guard lock(state->mutex);
    if (generation != state->generation) {
        return H264SubmitResult::InvalidGeneration;
    }
    if (state->closing) return H264SubmitResult::Closed;

    if (state->waitingForKeyframe && !accessUnit.keyFrame) {
        ++state->droppedAccessUnits;
        return H264SubmitResult::DroppedUntilKeyframe;
    }

    bool acceptedAfterDrop = false;
    if (state->queue.size() >= kQueueCapacity) {
        state->droppedAccessUnits += state->queue.size() + 1;
        state->queue.clear();
        state->waitingForKeyframe = true;
        if (!accessUnit.keyFrame) {
            return H264SubmitResult::DroppedCapacity;
        }
        acceptedAfterDrop = true;
    } else if (state->waitingForKeyframe && accessUnit.keyFrame) {
        acceptedAfterDrop = true;
    }

    state->waitingForKeyframe = false;
    state->queue.push_back(std::move(accessUnit));
    ++state->acceptedAccessUnits;
    state->changed.notify_all();
    return acceptedAfterDrop
               ? H264SubmitResult::AcceptedAfterDrop
               : H264SubmitResult::Accepted;
}

} // namespace

class WebRtcEndpointSession::Impl final
{
public:
    explicit Impl(WebRtcSessionConfig configuration)
        : configuration_(std::move(configuration)),
          state_(std::make_shared<SharedState>())
    {
    }

    ~Impl()
    {
        close();
    }

    EndpointDescriptionResult createOffer(std::chrono::milliseconds timeout)
    {
        if (configuration_.signalingRole != SignalingRole::Offerer) {
            return {EndpointError::InvalidRole, {}, {}};
        }
        if (!initialize()) return {EndpointError::InvalidState, {}, {}};
        try {
            connection()->setLocalDescription(rtc::Description::Type::Offer);
        } catch (...) {
            fail();
            return {EndpointError::LibraryFailure, {}, {}};
        }
        return waitForDescription(timeout);
    }

    EndpointDescriptionResult acceptOfferAndCreateAnswer(
        const std::string &offerSdp,
        std::chrono::milliseconds timeout
    )
    {
        if (configuration_.signalingRole != SignalingRole::Answerer) {
            return {EndpointError::InvalidRole, {}, {}};
        }
        if (offerSdp.empty() || !initialize()) {
            return {EndpointError::InvalidState, {}, {}};
        }
        try {
            connection()->setRemoteDescription(rtc::Description(offerSdp, "offer"));
            connection()->setLocalDescription(rtc::Description::Type::Answer);
        } catch (...) {
            fail();
            return {EndpointError::LibraryFailure, {}, {}};
        }
        return waitForDescription(timeout);
    }

    EndpointConnectionResult acceptAnswerAndWait(
        const std::string &answerSdp,
        std::chrono::milliseconds timeout
    )
    {
        if (configuration_.signalingRole != SignalingRole::Offerer) {
            return {EndpointError::InvalidRole, {}};
        }
        if (answerSdp.empty() || !connection()) {
            return {EndpointError::InvalidState, {}};
        }
        try {
            connection()->setRemoteDescription(rtc::Description(answerSdp, "answer"));
        } catch (...) {
            fail();
            return {EndpointError::LibraryFailure, {}};
        }
        return waitConnected(timeout);
    }

    EndpointConnectionResult waitConnected(std::chrono::milliseconds timeout)
    {
        if (!connection()) return {EndpointError::InvalidState, {}};
        std::unique_lock lock(state_->mutex);
        const bool signaled = state_->changed.wait_for(
            lock,
            timeout,
            [state = state_] {
                return state->connected || state->failed || state->closing;
            }
        );
        if (!signaled) return {EndpointError::ConnectionTimeout, {}};
        if (!state_->connected) return {EndpointError::ConnectionFailed, {}};
        const auto types = state_->localCandidateTypes;
        lock.unlock();
        return {EndpointError::None, types};
    }

    std::optional<H264SubmitPort> createSendPort()
    {
        if (configuration_.videoDirection != VideoDirection::SendOnly) {
            return std::nullopt;
        }
        const std::lock_guard lock(state_->mutex);
        if (state_->closing || !state_->localTrack) return std::nullopt;
        const std::uint64_t generation = state_->generation;
        const std::weak_ptr<SharedState> weakState(state_);
        return H264SubmitPort(
            [weakState, generation](H264AccessUnit accessUnit) {
                return enqueueAccessUnit(
                    weakState, generation, std::move(accessUnit)
                );
            }
        );
    }

    EndpointSnapshot snapshot() const noexcept
    {
        const std::lock_guard lock(state_->mutex);
        EndpointSnapshot result;
        result.state = state_->endpointState;
        result.generation = state_->generation;
        result.queueDepth = state_->queue.size();
        result.acceptedAccessUnits = state_->acceptedAccessUnits;
        result.droppedAccessUnits = state_->droppedAccessUnits;
        result.sentAccessUnits = state_->sentAccessUnits;
        result.receivedRtpPackets = state_->receivedRtpPackets;
        result.sendFailures = state_->sendFailures;
        result.waitingForKeyframe = state_->waitingForKeyframe;
        try {
            result.trackOpen = state_->localTrack && state_->localTrack->isOpen();
        } catch (...) {
            result.trackOpen = false;
        }
        return result;
    }

    void beginClose() noexcept
    {
        const std::lock_guard lock(state_->mutex);
        if (state_->closing) return;
        state_->closing = true;
        ++state_->generation;
        state_->endpointState = EndpointState::Closing;
        state_->changed.notify_all();
    }

    void close() noexcept
    {
        beginClose();
        if (sender_.joinable()) sender_.join();

        std::shared_ptr<rtc::PeerConnection> connectionValue;
        std::shared_ptr<rtc::Track> localTrack;
        std::shared_ptr<rtc::Track> remoteTrack;
        {
            const std::lock_guard lock(state_->mutex);
            connectionValue = std::move(state_->connection);
            localTrack = std::move(state_->localTrack);
            remoteTrack = std::move(state_->remoteTrack);
            state_->queue.clear();
        }
        try {
            if (localTrack) localTrack->resetCallbacks();
            if (remoteTrack) remoteTrack->resetCallbacks();
            if (connectionValue) connectionValue->resetCallbacks();
        } catch (...) {
        }
        try {
            if (localTrack) localTrack->close();
            if (remoteTrack && remoteTrack != localTrack) remoteTrack->close();
            if (connectionValue) connectionValue->close();
        } catch (...) {
        }
        {
            const std::lock_guard lock(state_->mutex);
            state_->endpointState = EndpointState::Closed;
            state_->changed.notify_all();
        }
    }

private:
    bool initialize()
    {
        {
            const std::lock_guard lock(state_->mutex);
            if (state_->connection || state_->closing) return false;
        }

        std::shared_ptr<rtc::PeerConnection> connectionValue;
        try {
            rtc::Configuration rtcConfiguration;
            rtcConfiguration.disableAutoNegotiation = true;
            rtcConfiguration.enableIceTcp = false;
            for (const IceServerRuntimeConfig &server : configuration_.ice.servers) {
                for (const std::string &url : server.urls) {
                    rtc::IceServer iceServer(url);
                    iceServer.username = server.username;
                    iceServer.password = server.password;
                    rtcConfiguration.iceServers.push_back(std::move(iceServer));
                }
            }
            connectionValue = std::make_shared<rtc::PeerConnection>(rtcConfiguration);
        } catch (...) {
            return false;
        }

        const std::weak_ptr<SharedState> weakState(state_);
        std::uint64_t generation = 0;
        {
            const std::lock_guard lock(state_->mutex);
            generation = state_->generation;
        }
        connectionValue->onGatheringStateChange(
            [weakState, generation](rtc::PeerConnection::GatheringState value) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing || state->generation != generation) return;
                state->endpointState = EndpointState::Gathering;
                state->gatheringComplete =
                    value == rtc::PeerConnection::GatheringState::Complete;
                state->changed.notify_all();
            }
        );
        connectionValue->onStateChange(
            [weakState, generation](rtc::PeerConnection::State value) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing || state->generation != generation) return;
                state->connected = value == rtc::PeerConnection::State::Connected;
                state->failed = value == rtc::PeerConnection::State::Failed ||
                                value == rtc::PeerConnection::State::Closed;
                if (state->connected) {
                    state->endpointState = EndpointState::Connected;
                } else if (state->failed) {
                    state->endpointState = EndpointState::Failed;
                } else {
                    state->endpointState = EndpointState::Connecting;
                }
                state->changed.notify_all();
            }
        );
        connectionValue->onTrack(
            [weakState, generation](std::shared_ptr<rtc::Track> track) {
                const auto state = weakState.lock();
                if (!state) return;
                {
                    const std::lock_guard lock(state->mutex);
                    if (!state->closing && state->generation == generation) {
                        state->remoteTrack = track;
                        state->changed.notify_all();
                    }
                }
                const auto current = weakState.lock();
                if (!current) return;
                {
                    const std::lock_guard lock(current->mutex);
                    if (current->closing || current->generation != generation) {
                        try {
                            track->close();
                        } catch (...) {
                        }
                        return;
                    }
                }
                track->onMessage(
                    [weakState, generation](rtc::binary) {
                        const auto callbackState = weakState.lock();
                        if (!callbackState) return;
                        const std::lock_guard lock(callbackState->mutex);
                        if (callbackState->closing ||
                            callbackState->generation != generation) return;
                        ++callbackState->receivedRtpPackets;
                        callbackState->changed.notify_all();
                    },
                    nullptr
                );
            }
        );

        try {
            const rtc::Description::Direction direction =
                configuration_.videoDirection == VideoDirection::SendOnly
                    ? rtc::Description::Direction::SendOnly
                    : rtc::Description::Direction::RecvOnly;
            rtc::Description::Video video("video", direction);
            video.addH264Codec(kPayloadType);
            if (direction == rtc::Description::Direction::SendOnly) {
                video.addSSRC(kSsrc, "rtmp-monitor-week4");
            }
            auto track = connectionValue->addTrack(video);
            if (direction == rtc::Description::Direction::SendOnly) {
                auto rtpConfiguration =
                    std::make_shared<rtc::RtpPacketizationConfig>(
                        kSsrc,
                        "rtmp-monitor-week4",
                        kPayloadType,
                        rtc::H264RtpPacketizer::ClockRate
                    );
                auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
                    rtc::NalUnit::Separator::StartSequence,
                    rtpConfiguration,
                    kMaximumFragmentBytes
                );
                packetizer->addToChain(
                    std::make_shared<rtc::RtcpSrReporter>(rtpConfiguration)
                );
                packetizer->addToChain(
                    std::make_shared<rtc::RtcpNackResponder>()
                );
                track->setMediaHandler(std::move(packetizer));
            } else {
                track->onMessage(
                    [weakState, generation](rtc::binary) {
                        const auto receiveState = weakState.lock();
                        if (!receiveState) return;
                        const std::lock_guard lock(receiveState->mutex);
                        if (receiveState->closing ||
                            receiveState->generation != generation) return;
                        ++receiveState->receivedRtpPackets;
                        receiveState->changed.notify_all();
                    },
                    nullptr
                );
            }
            const std::lock_guard lock(state_->mutex);
            state_->connection = std::move(connectionValue);
            state_->localTrack = std::move(track);
        } catch (...) {
            try {
                connectionValue->resetCallbacks();
                connectionValue->close();
            } catch (...) {
            }
            return false;
        }

        sender_ = std::thread([state = state_] { runSender(state); });
        return true;
    }

    static void runSender(const std::shared_ptr<SharedState> &state)
    {
        for (;;) {
            H264AccessUnit accessUnit;
            std::shared_ptr<rtc::Track> track;
            {
                std::unique_lock lock(state->mutex);
                state->changed.wait(lock, [state] {
                    return state->closing ||
                           (state->connected && !state->queue.empty());
                });
                if (state->closing) return;
                accessUnit = std::move(state->queue.front());
                state->queue.pop_front();
                track = state->localTrack;
            }

            try {
                if (!track || !track->isOpen()) throw std::runtime_error("track_closed");
                track->sendFrame(
                    reinterpret_cast<const std::byte *>(accessUnit.annexB.data()),
                    accessUnit.annexB.size(),
                    rtc::FrameInfo(std::chrono::duration<double, std::micro>(
                        static_cast<double>(accessUnit.mediaTimestampUs)
                    ))
                );
                const std::lock_guard lock(state->mutex);
                ++state->sentAccessUnits;
            } catch (...) {
                const std::lock_guard lock(state->mutex);
                ++state->sendFailures;
                state->waitingForKeyframe = true;
                state->queue.clear();
            }
        }
    }

    EndpointDescriptionResult waitForDescription(
        std::chrono::milliseconds timeout
    )
    {
        std::unique_lock lock(state_->mutex);
        const bool signaled = state_->changed.wait_for(
            lock,
            timeout,
            [state = state_] {
                return state->gatheringComplete || state->failed || state->closing;
            }
        );
        if (!signaled) return {EndpointError::GatheringTimeout, {}, {}};
        if (!state_->gatheringComplete || !state_->connection) {
            return {EndpointError::ConnectionFailed, {}, {}};
        }
        const auto connectionValue = state_->connection;
        lock.unlock();

        try {
            const auto description = connectionValue->localDescription();
            if (!description.has_value()) {
                return {EndpointError::LibraryFailure, {}, {}};
            }
            auto types = candidateTypes(*description);
            {
                const std::lock_guard stateLock(state_->mutex);
                state_->localCandidateTypes = types;
            }
            return {EndpointError::None, description->generateSdp(), std::move(types)};
        } catch (...) {
            return {EndpointError::LibraryFailure, {}, {}};
        }
    }

    std::shared_ptr<rtc::PeerConnection> connection() const
    {
        const std::lock_guard lock(state_->mutex);
        return state_->connection;
    }

    void fail() noexcept
    {
        const std::lock_guard lock(state_->mutex);
        if (state_->closing) return;
        state_->failed = true;
        state_->endpointState = EndpointState::Failed;
        state_->changed.notify_all();
    }

    WebRtcSessionConfig configuration_;
    std::shared_ptr<SharedState> state_;
    std::thread sender_;
};

WebRtcEndpointSession::WebRtcEndpointSession(WebRtcSessionConfig configuration)
    : impl_(std::make_unique<Impl>(std::move(configuration)))
{
}

WebRtcEndpointSession::~WebRtcEndpointSession() = default;

EndpointDescriptionResult WebRtcEndpointSession::createOffer(
    std::chrono::milliseconds timeout
)
{
    return impl_->createOffer(timeout);
}

EndpointDescriptionResult WebRtcEndpointSession::acceptOfferAndCreateAnswer(
    const std::string &offerSdp,
    std::chrono::milliseconds timeout
)
{
    return impl_->acceptOfferAndCreateAnswer(offerSdp, timeout);
}

EndpointConnectionResult WebRtcEndpointSession::acceptAnswerAndWait(
    const std::string &answerSdp,
    std::chrono::milliseconds timeout
)
{
    return impl_->acceptAnswerAndWait(answerSdp, timeout);
}

EndpointConnectionResult WebRtcEndpointSession::waitConnected(
    std::chrono::milliseconds timeout
)
{
    return impl_->waitConnected(timeout);
}

std::optional<H264SubmitPort> WebRtcEndpointSession::createSendPort()
{
    return impl_->createSendPort();
}

EndpointSnapshot WebRtcEndpointSession::snapshot() const noexcept
{
    return impl_->snapshot();
}

void WebRtcEndpointSession::beginClose() noexcept
{
    impl_->beginClose();
}

void WebRtcEndpointSession::close() noexcept
{
    impl_->close();
}

const char *WebRtcEndpointSession::errorName(EndpointError error) noexcept
{
    switch (error) {
    case EndpointError::None: return "none";
    case EndpointError::InvalidState: return "invalid_state";
    case EndpointError::InvalidRole: return "invalid_role";
    case EndpointError::LibraryFailure: return "library_failure";
    case EndpointError::GatheringTimeout: return "gathering_timeout";
    case EndpointError::ConnectionTimeout: return "connection_timeout";
    case EndpointError::ConnectionFailed: return "connection_failed";
    }
    return "unknown";
}

const char *WebRtcEndpointSession::stateName(EndpointState state) noexcept
{
    switch (state) {
    case EndpointState::New: return "new";
    case EndpointState::Gathering: return "gathering";
    case EndpointState::Connecting: return "connecting";
    case EndpointState::Connected: return "connected";
    case EndpointState::Closing: return "closing";
    case EndpointState::Closed: return "closed";
    case EndpointState::Failed: return "failed";
    }
    return "unknown";
}

} // namespace rtmp_monitor::webrtc_transport
