#pragma once

#include "signaling_session/DirectSessionCore.h"

#include <QString>

namespace rtmp::p2p {

enum class DirectRuntimeRole { Operator, Device };

struct DirectRuntimeConfig final {
    bool enabled{false};
    QString brokerUrl;
    QString clientId;
    DirectRuntimeRole role{DirectRuntimeRole::Operator};
    DirectRouteIdentity identity;
};

struct DirectRuntimeConfigResult final {
    bool ok{false};
    QString error;
    DirectRuntimeConfig config;
};

/** Reads explicit, Git-external Direct02 runtime configuration. */
DirectRuntimeConfigResult loadDirectRuntimeConfig(const QString &path,
                                                  DirectRuntimeRole expectedRole);

} // namespace rtmp::p2p
