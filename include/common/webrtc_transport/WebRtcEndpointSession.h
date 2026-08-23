#pragma once

#include "h264/H264MediaContracts.h"
#include "webrtc_contracts/WebRtcSessionContracts.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rtmp_monitor::webrtc_transport {

enum class EndpointError {
    None,
    InvalidState,
    InvalidRole,
    IncompatibleMedia,
    MissingReceiveSink,
    LibraryFailure,
    GatheringTimeout,
    ConnectionTimeout,
    ConnectionFailed,
};

enum class EndpointState {
    New,
    Gathering,
    Connecting,
    Connected,
    Closing,
    Closed,
    Failed,
};

struct EndpointDescriptionResult
{
    EndpointError error = EndpointError::None;
    std::string sdp;
    std::vector<std::string> candidateTypes;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == EndpointError::None && !sdp.empty();
    }
};

struct EndpointConnectionResult
{
    EndpointError error = EndpointError::None;
    std::vector<std::string> candidateTypes;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == EndpointError::None;
    }
};

struct EndpointSnapshot
{
    EndpointState state = EndpointState::New;
    std::uint64_t generation = 0;
    std::size_t queueDepth = 0;
    std::uint64_t acceptedAccessUnits = 0;
    std::uint64_t droppedAccessUnits = 0;
    std::uint64_t sentAccessUnits = 0;
    std::uint64_t receivedRtpPackets = 0;
    std::uint64_t receivedAccessUnits = 0;
    std::uint64_t submittedAccessUnits = 0;
    std::uint64_t receiveDrops = 0;
    std::uint64_t invalidAccessUnits = 0;
    std::uint64_t sendFailures = 0;
    bool waitingForKeyframe = false;
    bool trackOpen = false;
};

using H264SubmitPort = std::function<H264SubmitResult(H264AccessUnit)>;
using H264ReceiveSink =
    std::function<H264SubmitResult(SessionMediaSample)>;

/**
 * Owns one PeerConnection session, its video Track, generation and bounded
 * sender. Signaling file I/O and media sources remain composition-root duties.
 */
class WebRtcEndpointSession final
{
public:
    explicit WebRtcEndpointSession(WebRtcSessionConfig configuration);
    ~WebRtcEndpointSession();

    WebRtcEndpointSession(const WebRtcEndpointSession &) = delete;
    WebRtcEndpointSession &operator=(const WebRtcEndpointSession &) = delete;

    [[nodiscard]] EndpointDescriptionResult createOffer(
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );
    [[nodiscard]] EndpointDescriptionResult acceptOfferAndCreateAnswer(
        const std::string &offerSdp,
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );
    [[nodiscard]] EndpointConnectionResult acceptAnswerAndWait(
        const std::string &answerSdp,
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );
    [[nodiscard]] EndpointConnectionResult waitConnected(
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );
    [[nodiscard]] std::optional<H264SubmitPort> createSendPort();
    [[nodiscard]] EndpointError setReceiveSink(H264ReceiveSink sink);
    [[nodiscard]] EndpointSnapshot snapshot() const noexcept;

    void beginClose() noexcept;
    void close() noexcept;

    [[nodiscard]] static const char *errorName(EndpointError error) noexcept;
    [[nodiscard]] static const char *stateName(EndpointState state) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtmp_monitor::webrtc_transport
