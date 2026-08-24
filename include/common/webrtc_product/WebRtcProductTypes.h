#pragma once

#include "media/PlaybackTypes.h"
#include "webrtc_contracts/WebRtcSessionContracts.h"
#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace rtmp_monitor::webrtc_product {

/** Product-visible state for one runtime-only receive session. */
enum class WebRtcProductState {
    Idle,
    Connecting,
    Direct,
    NeedsRelay,
    Error,
};

/** Transient operational events. None of these imply device identity/control. */
enum class WebRtcProductEventKind {
    SessionStarted,
    DescriptionExported,
    DirectEstablished,
    MediaInterrupted,
    MediaRecovered,
    NeedsRelay,
    Failed,
    Cancelled,
};

/**
 * Runtime-only input for the Week 8 product receive path.
 *
 * The request deliberately has no peer/device identity, file path, RTMP URL,
 * saved-profile key or auto-connect flag. ICE material lives only for this
 * object lifetime and is never written by the product controller.
 */
struct WebRtcSessionRequest
{
    QString displayName = QStringLiteral("WebRTC 临时画面");
    SignalingRole signalingRole = SignalingRole::Answerer;
    IceRuntimeConfig ice;
};

struct WebRtcProductEvent
{
    WebRtcProductEventKind kind = WebRtcProductEventKind::SessionStarted;
    QString reason;
};

/** Read-only copies; reading diagnostics never retains protocol resources. */
struct WebRtcProductDiagnostics
{
    WebRtcProductState state = WebRtcProductState::Idle;
    rtmp_monitor::webrtc_transport::EndpointSnapshot transport;
    StreamMetrics media;
    qint64 presentedFrameAgeMs = -1;
    bool selectedNonRelayPair = false;
    bool controlAuthorized = false;
    bool rtmpFallbackStarted = false;
};

class WebRtcProductPolicy final
{
public:
    WebRtcProductPolicy() = delete;

    [[nodiscard]] static bool validateRequest(
        const WebRtcSessionRequest &request,
        QString *error = nullptr
    );
    [[nodiscard]] static bool selectedPairIsNonRelay(
        const rtmp_monitor::webrtc_transport::EndpointConnectionResult &result
    ) noexcept;
    [[nodiscard]] static bool hasFreshDirectEvidence(
        const rtmp_monitor::webrtc_transport::EndpointConnectionResult &result,
        const WebRtcProductDiagnostics &diagnostics,
        qint64 freshnessLimitMs = 1'000
    ) noexcept;
    [[nodiscard]] static WebRtcProductState classifyConnectionFailure(
        rtmp_monitor::webrtc_transport::EndpointError error,
        rtmp_monitor::webrtc_transport::EndpointIceState iceState,
        const std::vector<std::string> &candidateTypes
    ) noexcept;
    [[nodiscard]] static const char *stateName(
        WebRtcProductState state
    ) noexcept;
    [[nodiscard]] static const char *eventName(
        WebRtcProductEventKind kind
    ) noexcept;
};

} // namespace rtmp_monitor::webrtc_product
