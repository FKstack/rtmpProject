#include "media/EncodedVideoInputHandle.h"

#include "EncodedVideoInputControl.h"
#include "media/EncodedVideoDecodeSession.h"

#include <utility>

EncodedVideoInputHandle::EncodedVideoInputHandle(
    std::shared_ptr<EncodedVideoInputControl> control
)
    : control_(std::move(control))
{
}

EncodedVideoInputHandle::~EncodedVideoInputHandle()
{
    close();
}

EncodedVideoInputHandle::EncodedVideoInputHandle(
    EncodedVideoInputHandle &&other
) noexcept = default;

EncodedVideoInputHandle &EncodedVideoInputHandle::operator=(
    EncodedVideoInputHandle &&other
) noexcept
{
    if (this != &other) {
        close();
        control_ = std::move(other.control_);
    }
    return *this;
}

bool EncodedVideoInputHandle::isOpen() const noexcept
{
    return control_ != nullptr &&
           !control_->closed.load(std::memory_order_acquire) &&
           !control_->session.expired();
}

StreamId EncodedVideoInputHandle::streamId() const noexcept
{
    return control_ != nullptr ? control_->streamId : kInvalidStreamId;
}

std::uint64_t EncodedVideoInputHandle::generation() const noexcept
{
    return control_ != nullptr ? control_->generation : 0;
}

H264SubmitResult EncodedVideoInputHandle::submit(
    H264AccessUnit accessUnit
) const
{
    SessionMediaSample sample;
    sample.generation = generation();
    sample.accessUnit = std::move(accessUnit);
    return submit(std::move(sample));
}

H264SubmitResult EncodedVideoInputHandle::submit(
    SessionMediaSample sample
) const
{
    if (control_ == nullptr ||
        control_->closed.load(std::memory_order_acquire)) {
        return H264SubmitResult::Closed;
    }
    if (sample.generation == 0 ||
        sample.generation != control_->generation) {
        return H264SubmitResult::InvalidGeneration;
    }
    const auto session = control_->session.lock();
    return session != nullptr
        ? session->submit(std::move(sample))
        : H264SubmitResult::Closed;
}

void EncodedVideoInputHandle::close()
{
    if (control_ == nullptr ||
        control_->closed.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (const auto session = control_->session.lock(); session != nullptr) {
        session->closeGeneration(control_->generation);
    }
}
