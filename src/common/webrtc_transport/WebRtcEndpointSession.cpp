#include "webrtc_transport/WebRtcEndpointSession.h"

#include "webrtc_transport/H264ReceivePipeline.h"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

namespace rtmp_monitor::webrtc_transport {
namespace {

constexpr std::size_t kMaximumAccessUnitBytes =
    detail::H264ReceivePipeline::kMaximumAccessUnitBytes;
constexpr std::size_t kQueueCapacity = 2;
constexpr std::uint8_t kPayloadType = 102;
constexpr std::uint32_t kSsrc = 0x52544d50U;
constexpr std::size_t kMaximumFragmentBytes = 1200;

std::string trimAndLower(std::string value)
{
    const auto first = std::find_if_not(
        value.begin(), value.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }
    );
    const auto last = std::find_if_not(
        value.rbegin(), value.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }
    ).base();
    if (first >= last) return {};
    value = std::string(first, last);
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
    );
    return value;
}

std::map<std::string, std::string> parseFmtp(
    const std::vector<std::string> &fmtps
)
{
    std::map<std::string, std::string> values;
    for (const std::string &fmtp : fmtps) {
        std::size_t offset = 0;
        while (offset <= fmtp.size()) {
            const std::size_t end = fmtp.find(';', offset);
            const std::string token = fmtp.substr(
                offset,
                end == std::string::npos ? std::string::npos : end - offset
            );
            const std::size_t equals = token.find('=');
            if (equals != std::string::npos) {
                values[trimAndLower(token.substr(0, equals))] =
                    trimAndLower(token.substr(equals + 1));
            }
            if (end == std::string::npos) break;
            offset = end + 1;
        }
    }
    return values;
}

bool compatibleRemoteDescription(
    const rtc::Description &description,
    VideoDirection localDirection
)
{
    for (int index = 0; index < description.mediaCount(); ++index) {
        const auto entry = description.media(index);
        const auto mediaPointer =
            std::get_if<const rtc::Description::Media *>(&entry);
        if (mediaPointer == nullptr || *mediaPointer == nullptr) continue;
        const rtc::Description::Media &media = **mediaPointer;
        if (media.type() != "video" || media.mid() != "video") continue;

        const rtc::Description::Direction direction = media.direction();
        const bool directionCompatible =
            localDirection == VideoDirection::ReceiveOnly
                ? direction == rtc::Description::Direction::SendOnly ||
                      direction == rtc::Description::Direction::SendRecv
                : direction == rtc::Description::Direction::RecvOnly ||
                      direction == rtc::Description::Direction::SendRecv;
        if (!directionCompatible) return false;

        const rtc::Description::Media::RtpMap *rtp =
            media.rtpMap(kPayloadType);
        if (rtp == nullptr || trimAndLower(rtp->format) != "h264" ||
            rtp->clockRate !=
                static_cast<int>(rtc::H264RtpPacketizer::ClockRate)) {
            return false;
        }
        const auto fmtp = parseFmtp(rtp->fmtps);
        const auto matches = [&fmtp](
            std::string_view key,
            std::string_view value
        ) {
            const auto iterator = fmtp.find(std::string(key));
            return iterator != fmtp.end() && iterator->second == value;
        };
        return matches("profile-level-id", "42e01f") &&
               matches("packetization-mode", "1") &&
               matches("level-asymmetry-allowed", "1");
    }
    return false;
}

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

std::string selectedCandidateTypeName(rtc::Candidate::Type type)
{
    switch (type) {
    case rtc::Candidate::Type::Host: return "host";
    case rtc::Candidate::Type::ServerReflexive:
    case rtc::Candidate::Type::PeerReflexive: return "srflx";
    case rtc::Candidate::Type::Relayed: return "relay";
    case rtc::Candidate::Type::Unknown: break;
    }
    return "unknown";
}

std::string candidateTransportName(rtc::Candidate::TransportType type)
{
    switch (type) {
    case rtc::Candidate::TransportType::Udp: return "udp";
    case rtc::Candidate::TransportType::TcpActive:
    case rtc::Candidate::TransportType::TcpPassive:
    case rtc::Candidate::TransportType::TcpSo:
    case rtc::Candidate::TransportType::TcpUnknown: return "tcp";
    case rtc::Candidate::TransportType::Unknown: break;
    }
    return "unknown";
}

std::optional<EndpointCandidatePair> selectedCandidatePair(
    const std::shared_ptr<rtc::PeerConnection> &connection
)
{
    if (!connection) return std::nullopt;
    rtc::Candidate local;
    rtc::Candidate remote;
    if (!connection->getSelectedCandidatePair(&local, &remote)) {
        return std::nullopt;
    }
    EndpointCandidatePair pair {
        selectedCandidateTypeName(local.type()),
        selectedCandidateTypeName(remote.type()),
        candidateTransportName(local.transportType()),
        candidateTransportName(remote.transportType())
    };
    if (pair.localType == "unknown" || pair.remoteType == "unknown" ||
        pair.localTransport == "unknown" ||
        pair.remoteTransport == "unknown") {
        return std::nullopt;
    }
    return pair;
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

EndpointIceState endpointIceState(rtc::PeerConnection::IceState state)
{
    switch (state) {
    case rtc::PeerConnection::IceState::New: return EndpointIceState::New;
    case rtc::PeerConnection::IceState::Checking:
        return EndpointIceState::Checking;
    case rtc::PeerConnection::IceState::Connected:
        return EndpointIceState::Connected;
    case rtc::PeerConnection::IceState::Completed:
        return EndpointIceState::Completed;
    case rtc::PeerConnection::IceState::Failed:
        return EndpointIceState::Failed;
    case rtc::PeerConnection::IceState::Disconnected:
        return EndpointIceState::Disconnected;
    case rtc::PeerConnection::IceState::Closed:
        return EndpointIceState::Closed;
    }
    return EndpointIceState::New;
}

struct SharedState
{
    std::mutex mutex;
    std::mutex receiveCallbackMutex;
    std::condition_variable changed;
    EndpointState endpointState = EndpointState::New;
    EndpointIceState iceState = EndpointIceState::New;
    bool closing = false;
    bool gatheringComplete = false;
    bool connected = false;
    bool hasConnected = false;
    bool failed = false;
    bool waitingForKeyframe = false;
    std::uint64_t generation = 1;
    std::uint64_t acceptedAccessUnits = 0;
    std::uint64_t droppedAccessUnits = 0;
    std::uint64_t sentAccessUnits = 0;
    std::uint64_t receivedRtpPackets = 0;
    std::uint64_t receivedAccessUnits = 0;
    std::uint64_t submittedAccessUnits = 0;
    std::uint64_t receiveDrops = 0;
    std::uint64_t invalidAccessUnits = 0;
    std::uint64_t sendFailures = 0;
    H264ReceiveSink receiveSink;
    detail::H264ReceivePipeline receivePipeline;
    std::deque<H264AccessUnit> queue;
    std::shared_ptr<rtc::PeerConnection> connection;
    std::shared_ptr<rtc::Track> localTrack;
    std::shared_ptr<rtc::Track> remoteTrack;
    std::vector<std::string> localCandidateTypes;
};

class IncomingRtpCounter final : public rtc::MediaHandler
{
public:
    IncomingRtpCounter(
        std::weak_ptr<SharedState> state,
        std::uint64_t generation
    )
        : state_(std::move(state)), generation_(generation)
    {
    }

    void incoming(
        rtc::message_vector &messages,
        const rtc::message_callback &
    ) override
    {
        std::uint64_t count = 0;
        for (const rtc::message_ptr &message : messages) {
            if (!message || message->type != rtc::Message::Binary ||
                message->size() < sizeof(rtc::RtpHeader)) {
                continue;
            }
            const auto *header = reinterpret_cast<const rtc::RtpHeader *>(
                message->data()
            );
            if (header->version() == 2 &&
                header->payloadType() == kPayloadType) {
                ++count;
            }
        }
        if (count == 0) return;
        const auto state = state_.lock();
        if (!state) return;
        const std::lock_guard lock(state->mutex);
        if (state->closing || state->generation != generation_) return;
        state->receivedRtpPackets += count;
        state->changed.notify_all();
    }

private:
    std::weak_ptr<SharedState> state_;
    std::uint64_t generation_ = 0;
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
            return {EndpointError::InvalidRole, {}, {}, EndpointIceState::New};
        }
        const EndpointError initialized = initialize();
        if (initialized != EndpointError::None) {
            return {initialized, {}, {}, snapshot().iceState};
        }
        try {
            connection()->setLocalDescription(rtc::Description::Type::Offer);
        } catch (...) {
            fail();
            return {EndpointError::LibraryFailure, {}, {}, snapshot().iceState};
        }
        return waitForDescription(timeout);
    }

    EndpointDescriptionResult acceptOfferAndCreateAnswer(
        const std::string &offerSdp,
        std::chrono::milliseconds timeout
    )
    {
        if (configuration_.signalingRole != SignalingRole::Answerer) {
            return {EndpointError::InvalidRole, {}, {}, EndpointIceState::New};
        }
        if (offerSdp.empty()) {
            return {EndpointError::InvalidState, {}, {}, EndpointIceState::New};
        }
        try {
            const rtc::Description offer(offerSdp, "offer");
            if (!compatibleRemoteDescription(
                    offer, configuration_.videoDirection)) {
                return {EndpointError::IncompatibleMedia, {}, {}, EndpointIceState::New};
            }
            const EndpointError initialized = initialize();
            if (initialized != EndpointError::None) {
                return {initialized, {}, {}, snapshot().iceState};
            }
            connection()->setRemoteDescription(offer);
            connection()->setLocalDescription(rtc::Description::Type::Answer);
        } catch (...) {
            fail();
            return {EndpointError::LibraryFailure, {}, {}, snapshot().iceState};
        }
        return waitForDescription(timeout);
    }

    EndpointConnectionResult acceptAnswerAndWait(
        const std::string &answerSdp,
        std::chrono::milliseconds timeout
    )
    {
        if (configuration_.signalingRole != SignalingRole::Offerer) {
            return {EndpointError::InvalidRole, {}, {}, EndpointIceState::New};
        }
        if (answerSdp.empty() || !connection()) {
            return {EndpointError::InvalidState, {}, {}, EndpointIceState::New};
        }
        try {
            const rtc::Description answer(answerSdp, "answer");
            if (!compatibleRemoteDescription(
                    answer, configuration_.videoDirection)) {
                return {EndpointError::IncompatibleMedia, {}, {}, snapshot().iceState};
            }
            connection()->setRemoteDescription(answer);
        } catch (...) {
            fail();
            return {EndpointError::LibraryFailure, {}, {}, snapshot().iceState};
        }
        return waitConnected(timeout);
    }

    EndpointConnectionResult waitConnected(std::chrono::milliseconds timeout)
    {
        if (!connection()) {
            return {EndpointError::InvalidState, {}, {}, snapshot().iceState};
        }
        std::unique_lock lock(state_->mutex);
        const bool signaled = state_->changed.wait_for(
            lock,
            timeout,
            [state = state_] {
                return state->connected || state->failed || state->closing;
            }
        );
        const auto types = state_->localCandidateTypes;
        const EndpointIceState iceState = state_->iceState;
        if (!signaled) {
            return {EndpointError::ConnectionTimeout, types, {}, iceState};
        }
        if (!state_->connected) {
            return {EndpointError::ConnectionFailed, types, {}, iceState};
        }
        const auto connectionValue = state_->connection;
        lock.unlock();
        std::optional<EndpointCandidatePair> pair;
        try {
            pair = selectedCandidatePair(connectionValue);
        } catch (...) {
            pair.reset();
        }
        return {EndpointError::None, types, std::move(pair), iceState};
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

    EndpointError setReceiveSink(H264ReceiveSink sink)
    {
        if (configuration_.videoDirection != VideoDirection::ReceiveOnly) {
            return EndpointError::InvalidRole;
        }
        if (!sink) return EndpointError::MissingReceiveSink;
        const std::lock_guard lock(state_->mutex);
        if (state_->closing || state_->connection ||
            state_->endpointState != EndpointState::New) {
            return EndpointError::InvalidState;
        }
        state_->receiveSink = std::move(sink);
        return EndpointError::None;
    }

    EndpointSnapshot snapshot() const noexcept
    {
        const std::lock_guard lock(state_->mutex);
        EndpointSnapshot result;
        result.state = state_->endpointState;
        result.iceState = state_->iceState;
        result.generation = state_->generation;
        result.queueDepth = state_->queue.size();
        result.acceptedAccessUnits = state_->acceptedAccessUnits;
        result.droppedAccessUnits = state_->droppedAccessUnits;
        result.sentAccessUnits = state_->sentAccessUnits;
        result.receivedRtpPackets = state_->receivedRtpPackets;
        result.receivedAccessUnits = state_->receivedAccessUnits;
        result.submittedAccessUnits = state_->submittedAccessUnits;
        result.receiveDrops = state_->receiveDrops;
        result.invalidAccessUnits = state_->invalidAccessUnits;
        result.sendFailures = state_->sendFailures;
        result.waitingForKeyframe =
            configuration_.videoDirection == VideoDirection::ReceiveOnly
                ? state_->receivePipeline.waitingForKeyframe()
                : state_->waitingForKeyframe;
        try {
            result.trackOpen = state_->localTrack && state_->localTrack->isOpen();
        } catch (...) {
            result.trackOpen = false;
        }
        return result;
    }

    void beginClose() noexcept
    {
        const std::lock_guard callbackLock(state_->receiveCallbackMutex);
        const std::lock_guard lock(state_->mutex);
        if (state_->closing) return;
        state_->closing = true;
        ++state_->generation;
        state_->receiveSink = {};
        state_->receivePipeline.resetGeneration(state_->generation);
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
    EndpointError initialize()
    {
        {
            const std::lock_guard lock(state_->mutex);
            if (state_->connection || state_->closing) {
                return EndpointError::InvalidState;
            }
            if (configuration_.videoDirection == VideoDirection::ReceiveOnly &&
                !state_->receiveSink) {
                return EndpointError::MissingReceiveSink;
            }
        }

        rtc::Configuration rtcConfiguration;
        try {
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
        } catch (...) {
            return EndpointError::InvalidIceConfiguration;
        }

        std::shared_ptr<rtc::PeerConnection> connectionValue;
        try {
            connectionValue = std::make_shared<rtc::PeerConnection>(rtcConfiguration);
        } catch (...) {
            return EndpointError::LibraryFailure;
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
        connectionValue->onLocalCandidate(
            [weakState, generation](rtc::Candidate candidate) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::string type = candidateTypeName(candidate.type());
                const std::lock_guard lock(state->mutex);
                if (state->closing || state->generation != generation) return;
                if (std::find(
                        state->localCandidateTypes.begin(),
                        state->localCandidateTypes.end(),
                        type
                    ) == state->localCandidateTypes.end()) {
                    state->localCandidateTypes.push_back(type);
                    std::sort(
                        state->localCandidateTypes.begin(),
                        state->localCandidateTypes.end()
                    );
                }
                state->changed.notify_all();
            }
        );
        connectionValue->onIceStateChange(
            [weakState, generation](rtc::PeerConnection::IceState value) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing || state->generation != generation) return;
                state->iceState = endpointIceState(value);
                state->changed.notify_all();
            }
        );
        connectionValue->onStateChange(
            [weakState, generation](rtc::PeerConnection::State value) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing || state->generation != generation) return;
                const bool nowConnected =
                    value == rtc::PeerConnection::State::Connected;
                const bool terminal =
                    value == rtc::PeerConnection::State::Failed ||
                    value == rtc::PeerConnection::State::Closed ||
                    (state->hasConnected &&
                     value == rtc::PeerConnection::State::Disconnected);
                state->connected = nowConnected;
                if (nowConnected) state->hasConnected = true;
                state->failed = terminal;
                if (nowConnected) {
                    state->endpointState = EndpointState::Connected;
                } else if (terminal) {
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
                    if (state->closing || state->generation != generation) {
                        track.reset();
                    } else {
                        state->remoteTrack = track;
                        state->changed.notify_all();
                    }
                }
                try {
                    if (track) {
                        track->resetCallbacks();
                        track->close();
                    }
                } catch (...) {
                }
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
                auto depacketizer =
                    std::make_shared<rtc::H264RtpDepacketizer>(
                        rtc::NalUnit::Separator::StartSequence
                    );
                depacketizer->addToChain(
                    std::make_shared<rtc::RtcpReceivingSession>()
                );
                depacketizer->addToChain(
                    std::make_shared<IncomingRtpCounter>(
                        weakState, generation
                    )
                );
                track->setMediaHandler(std::move(depacketizer));
                track->onFrame(
                    [weakState, generation](
                        rtc::binary data,
                        rtc::FrameInfo info
                    ) {
                        handleReceivedFrame(
                            weakState,
                            generation,
                            std::move(data),
                            info.timestamp
                        );
                    }
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
            return EndpointError::LibraryFailure;
        }

        if (configuration_.videoDirection == VideoDirection::SendOnly) {
            sender_ = std::thread([state = state_] { runSender(state); });
        }
        return EndpointError::None;
    }

    static void handleReceivedFrame(
        const std::weak_ptr<SharedState> &weakState,
        std::uint64_t generation,
        rtc::binary data,
        std::uint32_t rtpTimestamp
    )
    {
        const auto state = weakState.lock();
        if (!state) return;

        std::vector<std::uint8_t> bytes;
        bytes.reserve(data.size());
        for (const std::byte value : data) {
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        }

        const std::lock_guard callbackLock(state->receiveCallbackMutex);
        H264ReceiveSink sink;
        std::optional<SessionMediaSample> sample;
        {
            const std::lock_guard lock(state->mutex);
            if (state->closing || state->generation != generation) return;
            ++state->receivedAccessUnits;
            auto result = state->receivePipeline.process(
                generation, std::move(bytes), rtpTimestamp
            );
            if (result.status ==
                detail::H264ReceivePipelineStatus::InvalidAccessUnit) {
                ++state->invalidAccessUnits;
                ++state->receiveDrops;
                state->changed.notify_all();
                return;
            }
            if (result.status ==
                detail::H264ReceivePipelineStatus::WaitingForKeyframe) {
                ++state->receiveDrops;
                state->changed.notify_all();
                return;
            }
            sink = state->receiveSink;
            sample = std::move(result.sample);
        }

        H264SubmitResult submitted = H264SubmitResult::Closed;
        try {
            if (sink && sample.has_value()) {
                submitted = sink(std::move(*sample));
            }
        } catch (...) {
            submitted = H264SubmitResult::ResourceFailure;
        }

        const std::lock_guard lock(state->mutex);
        if (state->closing || state->generation != generation) return;
        switch (submitted) {
        case H264SubmitResult::Accepted:
        case H264SubmitResult::AcceptedAfterDrop:
            ++state->submittedAccessUnits;
            break;
        case H264SubmitResult::DroppedCapacity:
        case H264SubmitResult::DroppedUntilKeyframe:
        case H264SubmitResult::Closed:
        case H264SubmitResult::InvalidGeneration:
        case H264SubmitResult::InvalidAccessUnit:
        case H264SubmitResult::ResourceFailure:
            ++state->receiveDrops;
            state->receivePipeline.resetRecovery();
            break;
        }
        state->changed.notify_all();
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
        const auto types = state_->localCandidateTypes;
        const EndpointIceState iceState = state_->iceState;
        if (!signaled) {
            return {EndpointError::GatheringTimeout, {}, types, iceState};
        }
        if (!state_->gatheringComplete || !state_->connection) {
            return {EndpointError::ConnectionFailed, {}, types, iceState};
        }
        const auto connectionValue = state_->connection;
        lock.unlock();

        try {
            const auto description = connectionValue->localDescription();
            if (!description.has_value()) {
                return {EndpointError::LibraryFailure, {}, types, iceState};
            }
            auto descriptionTypes = candidateTypes(*description);
            {
                const std::lock_guard stateLock(state_->mutex);
                state_->localCandidateTypes = descriptionTypes;
            }
            return {
                EndpointError::None,
                description->generateSdp(),
                std::move(descriptionTypes),
                iceState
            };
        } catch (...) {
            return {EndpointError::LibraryFailure, {}, types, iceState};
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

EndpointError WebRtcEndpointSession::setReceiveSink(H264ReceiveSink sink)
{
    return impl_->setReceiveSink(std::move(sink));
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
    case EndpointError::IncompatibleMedia: return "incompatible_media";
    case EndpointError::MissingReceiveSink: return "missing_receive_sink";
    case EndpointError::LibraryFailure: return "library_failure";
    case EndpointError::GatheringTimeout: return "gathering_timeout";
    case EndpointError::ConnectionTimeout: return "connection_timeout";
    case EndpointError::ConnectionFailed: return "connection_failed";
    case EndpointError::InvalidIceConfiguration:
        return "invalid_ice_config";
    }
    return "unknown";
}

const char *WebRtcEndpointSession::iceStateName(
    EndpointIceState state
) noexcept
{
    switch (state) {
    case EndpointIceState::New: return "new";
    case EndpointIceState::Checking: return "checking";
    case EndpointIceState::Connected: return "connected";
    case EndpointIceState::Completed: return "completed";
    case EndpointIceState::Failed: return "failed";
    case EndpointIceState::Disconnected: return "disconnected";
    case EndpointIceState::Closed: return "closed";
    }
    return "new";
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
