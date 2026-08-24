#pragma once

#include "webrtc_contracts/WebRtcSessionContracts.h"

#include <QCommandLineParser>

#include <chrono>
#include <optional>

namespace rtmp_monitor::webrtc_client {

enum class ClientMediaRole {
    Publisher,
    Viewer,
};

enum class ClientIceMode {
    HostOnly,
    Stun,
};

struct WebRtcClientOptions
{
    ClientMediaRole mediaRole = ClientMediaRole::Publisher;
    SignalingRole signalingRole = SignalingRole::Offerer;
    ClientIceMode iceMode = ClientIceMode::HostOnly;
    std::chrono::milliseconds timeout {30'000};

    static void configureParser(QCommandLineParser &parser);
    [[nodiscard]] static std::optional<WebRtcClientOptions> fromParser(
        const QCommandLineParser &parser
    );
};

[[nodiscard]] QString mediaRoleName(ClientMediaRole role);
[[nodiscard]] QString signalingRoleName(SignalingRole role);
[[nodiscard]] QString iceModeName(ClientIceMode mode);

} // namespace rtmp_monitor::webrtc_client
