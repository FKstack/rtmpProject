#include "render/VideoRenderController.h"

#include <utility>

VideoRenderController::VideoRenderController()
    : dirtyState_(std::make_shared<RenderDirtyState>())
{
}

VideoRenderController::~VideoRenderController()
{
    clearStreams();
}

void VideoRenderController::registerStream(
    StreamId streamId,
    std::shared_ptr<LatestFrameMailbox> mailbox
)
{
    unregisterStream(streamId);
    if (streamId == kInvalidStreamId || mailbox == nullptr) {
        return;
    }
    const std::weak_ptr<RenderDirtyState> weakDirty = dirtyState_;
    const auto subscription = mailbox->subscribe([weakDirty] {
        if (const auto dirty = weakDirty.lock(); dirty != nullptr) {
            dirty->mark(RenderDirtyFlag::Frame);
        }
    });
    bindings_.emplace(streamId, Binding {std::move(mailbox), subscription});
    dirtyState_->mark(RenderDirtyFlag::Resource);
}

void VideoRenderController::unregisterStream(StreamId streamId)
{
    const auto iterator = bindings_.find(streamId);
    if (iterator == bindings_.end()) {
        return;
    }
    iterator->second.mailbox->unsubscribe(iterator->second.subscription);
    bindings_.erase(iterator);
    dirtyState_->mark(RenderDirtyFlag::Resource);
}

void VideoRenderController::clearStreams()
{
    for (auto &entry : bindings_) {
        entry.second.mailbox->unsubscribe(entry.second.subscription);
    }
    bindings_.clear();
    dirtyState_->mark(RenderDirtyFlag::Resource);
}

void VideoRenderController::setSnapshot(RenderSnapshot snapshot)
{
    snapshot_ = std::move(snapshot);
    dirtyState_->mark(RenderDirtyFlag::Layout);
}

const RenderSnapshot &VideoRenderController::snapshot() const noexcept
{
    return snapshot_;
}

std::optional<VideoFrame> VideoRenderController::consumeFrame(
    StreamId streamId,
    std::uint64_t lastSequence
)
{
    const auto iterator = bindings_.find(streamId);
    return iterator != bindings_.end()
               ? iterator->second.mailbox->consumeLatestAfter(lastSequence)
               : std::nullopt;
}

std::shared_ptr<LatestFrameMailbox> VideoRenderController::mailbox(
    StreamId streamId
) const
{
    const auto iterator = bindings_.find(streamId);
    return iterator != bindings_.end() ? iterator->second.mailbox : nullptr;
}

void VideoRenderController::markDirty(RenderDirtyFlag flag) noexcept
{
    dirtyState_->mark(flag);
}

RenderDirtyFlags VideoRenderController::pendingDirty() const noexcept
{
    return dirtyState_->pending();
}

RenderDirtyFlags VideoRenderController::consumeDirty() noexcept
{
    return dirtyState_->consume();
}

std::shared_ptr<RenderDirtyState> VideoRenderController::dirtyState() const noexcept
{
    return dirtyState_;
}
