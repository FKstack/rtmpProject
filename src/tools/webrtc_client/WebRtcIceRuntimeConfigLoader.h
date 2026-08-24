#pragma once

#include "webrtc_contracts/WebRtcSessionContracts.h"

#include <QString>

namespace rtmp_monitor::webrtc_client {

enum class IceConfigLoadError {
    None,
    NotFound,
    ReadFailed,
    InvalidConfiguration,
    UnsupportedVersion,
};

struct IceConfigLoadResult
{
    IceConfigLoadError error = IceConfigLoadError::None;
    IceRuntimeConfig configuration;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == IceConfigLoadError::None;
    }
};

/** Reads the one fixed, runtime-only Week 7 STUN configuration. */
class WebRtcIceRuntimeConfigLoader final
{
public:
    [[nodiscard]] static IceConfigLoadResult load(const QString &path);
    [[nodiscard]] static const char *errorName(IceConfigLoadError error) noexcept;
};

} // namespace rtmp_monitor::webrtc_client
