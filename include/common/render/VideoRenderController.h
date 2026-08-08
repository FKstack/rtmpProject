#pragma once

#include <memory>
#include <optional>
#include <unordered_map>

#include "media/LatestFrameMailbox.h"
#include "render/RenderTypes.h"

/**
 * @brief Owns low-frequency render state and mailbox registrations for one canvas.
 */
class VideoRenderController final
{
public:
    VideoRenderController();
    ~VideoRenderController();

    VideoRenderController(const VideoRenderController &) = delete;
    VideoRenderController &operator=(const VideoRenderController &) = delete;

    void registerStream(
        StreamId streamId,
        std::shared_ptr<LatestFrameMailbox> mailbox
    );
    void unregisterStream(StreamId streamId);
    void clearStreams();

    void setSnapshot(RenderSnapshot snapshot);
    [[nodiscard]] const RenderSnapshot &snapshot() const noexcept;
    [[nodiscard]] std::optional<VideoFrame> consumeFrame(
        StreamId streamId,
        std::uint64_t lastSequence
    );
    [[nodiscard]] std::shared_ptr<LatestFrameMailbox> mailbox(
        StreamId streamId
    ) const;

    void markDirty(RenderDirtyFlag flag) noexcept;
    [[nodiscard]] RenderDirtyFlags pendingDirty() const noexcept;
    [[nodiscard]] RenderDirtyFlags consumeDirty() noexcept;
    [[nodiscard]] std::shared_ptr<RenderDirtyState> dirtyState() const noexcept;

private:
    struct Binding
    {
        std::shared_ptr<LatestFrameMailbox> mailbox;
        LatestFrameMailbox::SubscriberId subscription = 0;
    };

    std::unordered_map<StreamId, Binding> bindings_;
    RenderSnapshot snapshot_;
    std::shared_ptr<RenderDirtyState> dirtyState_;
};
