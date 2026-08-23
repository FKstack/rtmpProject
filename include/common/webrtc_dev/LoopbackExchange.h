#pragma once

#include "webrtc_dev/PeerConnectionProbe.h"
#include "webrtc_dev/SessionPackage.h"

#include <QStringList>

namespace rtmp_monitor::webrtc_dev {

struct LoopbackResult
{
    SessionError sessionError = SessionError::None;
    ProbeError probeError = ProbeError::None;
    int completedCycles = 0;
    QStringList candidateTypes;

    [[nodiscard]] bool ok() const noexcept
    {
        return sessionError == SessionError::None &&
               probeError == ProbeError::None;
    }
};

[[nodiscard]] LoopbackResult runLoopbackExchange(
    SessionPackageStore &store,
    int cycles = 1
);

} // namespace rtmp_monitor::webrtc_dev
