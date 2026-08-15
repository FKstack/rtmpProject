#pragma once

#include <QtGlobal>

#include <cstdint>
#include <memory>

#include "media/PlaybackTypes.h"

/** Internal media boundary. submitPacket always takes AVPacket ownership. */
class AudioPacketSink
{
public:
    virtual ~AudioPacketSink() = default;
    virtual void submitAudioConfiguration(
        StreamId streamId,
        const std::shared_ptr<void> &configuration,
        std::uint64_t sessionId
    ) = 0;
    virtual void submitAudioPacket(
        StreamId streamId,
        void *packet,
        qint64 receivedMonotonicMs,
        std::uint64_t sessionId
    ) = 0;
    virtual void invalidateAudioSession(
        StreamId streamId,
        std::uint64_t sessionId
    ) = 0;
};
