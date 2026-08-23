#pragma once

#include <cstdint>
#include <memory>

#include "h264/H264MediaContracts.h"
#include "media/PlaybackTypes.h"

struct EncodedVideoInputControl;
class MultiStreamPlaybackManager;

/**
 * @brief Move-only generation handle for one external H.264 decode ingress.
 *
 * Destroying or closing the handle invalidates its generation before the
 * decoder queue is drained. The manager remains the owner of the visible
 * StreamId and removes it through the existing removeStream() façade.
 */
class EncodedVideoInputHandle final
{
public:
    EncodedVideoInputHandle() = default;
    ~EncodedVideoInputHandle();

    EncodedVideoInputHandle(EncodedVideoInputHandle &&other) noexcept;
    EncodedVideoInputHandle &operator=(
        EncodedVideoInputHandle &&other
    ) noexcept;

    EncodedVideoInputHandle(const EncodedVideoInputHandle &) = delete;
    EncodedVideoInputHandle &operator=(
        const EncodedVideoInputHandle &
    ) = delete;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] StreamId streamId() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;

    /** Stamps this handle's generation into a new session envelope. */
    [[nodiscard]] H264SubmitResult submit(H264AccessUnit accessUnit) const;

    /** Allows a composition root to forward an existing session envelope. */
    [[nodiscard]] H264SubmitResult submit(SessionMediaSample sample) const;

    void close();

private:
    friend class MultiStreamPlaybackManager;
    explicit EncodedVideoInputHandle(
        std::shared_ptr<EncodedVideoInputControl> control
    );

    std::shared_ptr<EncodedVideoInputControl> control_;
};
