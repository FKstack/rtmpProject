#pragma once

#include <cstdint>
#include <memory>

#include "media/EncodedVideoDecodeSession.h"

/** Implementation-only bridge used by the RTMP input façade. */
struct EncodedVideoDecodeSessionFfmpegAccess
{
    static void attachPool(
        EncodedVideoDecodeSession &session,
        DecodeWorkerPool *decodeWorkerPool
    )
    {
        session.attachDecodeWorkerPool(decodeWorkerPool);
    }

    static void prepare(
        EncodedVideoDecodeSession &session,
        std::uint64_t generation
    )
    {
        session.prepareGeneration(generation, false);
    }

    static void configure(
        EncodedVideoDecodeSession &session,
        const std::shared_ptr<void> &configuration,
        std::uint64_t generation
    )
    {
        session.configureOpaque(configuration, generation, false);
    }

    static H264SubmitResult submitPacket(
        EncodedVideoDecodeSession &session,
        void *packet,
        qint64 receivedMonotonicMs,
        std::uint64_t generation
    )
    {
        return session.submitOpaquePacket(
            packet, receivedMonotonicMs, generation
        );
    }
};
