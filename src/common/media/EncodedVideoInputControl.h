#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "media/PlaybackTypes.h"

class EncodedVideoDecodeSession;

struct EncodedVideoInputControl
{
    std::weak_ptr<EncodedVideoDecodeSession> session;
    StreamId streamId = kInvalidStreamId;
    std::uint64_t generation = 0;
    std::atomic_bool closed {false};
};
