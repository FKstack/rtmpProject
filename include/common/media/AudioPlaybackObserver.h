#pragma once

#include <QtGlobal>

#include <cstdint>

#include "media/PlaybackTypes.h"

/**
 * Developer-only observation sample emitted after one decoded PCM frame has
 * been accepted by QAudioSink. The observer never owns playback resources and
 * must keep callbacks non-blocking.
 */
struct AudioPlaybackProbeSample
{
    StreamId streamId = kInvalidStreamId;
    std::uint64_t sessionGeneration = 0;
    qint64 mediaPtsUs = -1;
    qint64 videoRenderedPtsUs = -1;
    qint64 packetReceivedMonotonicUs = -1;
    qint64 decodedMonotonicUs = -1;
    qint64 queuedMonotonicUs = -1;
    qint64 sinkWriteMonotonicUs = -1;
    qint64 pcmBytes = 0;
};

class AudioPlaybackObserver
{
public:
    virtual ~AudioPlaybackObserver() = default;
    virtual void onAudioPlaybackSample(
        const AudioPlaybackProbeSample &sample
    ) noexcept = 0;
};
