#pragma once

#include <string>
#include <vector>

/** Signaling negotiation role; it does not imply a media direction. */
enum class SignalingRole {
    Offerer,
    Answerer,
};

/** First-stage media direction. Bidirectional video is intentionally absent. */
enum class VideoDirection {
    SendOnly,
    ReceiveOnly,
};

/** Runtime-only ICE server material. It must not be persisted in stream profiles. */
struct IceServerRuntimeConfig
{
    std::vector<std::string> urls;
    std::string username;
    std::string password;
};

struct IceRuntimeConfig
{
    std::vector<IceServerRuntimeConfig> servers;
};

/**
 * @brief Minimal endpoint-session configuration frozen for the two-client path.
 *
 * SDP, peer identity, device identity and UI objects are deliberately not part
 * of this value. Session generation belongs to SessionMediaSample instead.
 */
struct WebRtcSessionConfig
{
    SignalingRole signalingRole = SignalingRole::Offerer;
    VideoDirection videoDirection = VideoDirection::SendOnly;
    IceRuntimeConfig ice;
};
