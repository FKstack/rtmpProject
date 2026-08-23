#include "webrtc_dev/PeerConnectionProbe.h"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace rtmp_monitor::webrtc_dev {
namespace {

QString candidateTypeName(rtc::Candidate::Type type)
{
    switch (type) {
    case rtc::Candidate::Type::Host: return QStringLiteral("host");
    case rtc::Candidate::Type::ServerReflexive: return QStringLiteral("srflx");
    case rtc::Candidate::Type::PeerReflexive: return QStringLiteral("prflx");
    case rtc::Candidate::Type::Relayed: return QStringLiteral("relay");
    case rtc::Candidate::Type::Unknown: break;
    }
    return QStringLiteral("unknown");
}

QStringList candidateTypes(const rtc::Description &description)
{
    QStringList result;
    for (const rtc::Candidate &candidate : description.candidates()) {
        const QString type = candidateTypeName(candidate.type());
        if (!result.contains(type)) result.push_back(type);
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct SharedState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool closing = false;
    bool gatheringComplete = false;
    bool connected = false;
    bool failed = false;
    rtc::PeerConnection::State state = rtc::PeerConnection::State::New;
    rtc::PeerConnection::IceState iceState =
        rtc::PeerConnection::IceState::New;
    std::shared_ptr<rtc::DataChannel> dataChannel;
};

} // namespace

class PeerConnectionProbe::Impl final
{
public:
    Impl() : state_(std::make_shared<SharedState>()) {}

    ~Impl()
    {
        close();
    }

    ProbeDescriptionResult createOffer(std::chrono::milliseconds timeout)
    {
        if (!initialize()) return {ProbeError::InvalidState, {}, {}};
        try {
            auto channel = connection_->createDataChannel("week2-probe");
            {
                const std::lock_guard lock(state_->mutex);
                state_->dataChannel = std::move(channel);
            }
            connection_->setLocalDescription(rtc::Description::Type::Offer);
        } catch (...) {
            close();
            return {ProbeError::LibraryFailure, {}, {}};
        }
        return waitForDescription(timeout);
    }

    ProbeDescriptionResult createAnswer(
        const QString &offerSdp,
        std::chrono::milliseconds timeout
    )
    {
        if (!initialize()) return {ProbeError::InvalidState, {}, {}};
        try {
            connection_->setRemoteDescription(
                rtc::Description(offerSdp.toStdString(), "offer")
            );
            connection_->setLocalDescription(rtc::Description::Type::Answer);
        } catch (...) {
            close();
            return {ProbeError::LibraryFailure, {}, {}};
        }
        return waitForDescription(timeout);
    }

    ProbeConnectionResult applyAnswerAndWait(
        const QString &answerSdp,
        std::chrono::milliseconds timeout
    )
    {
        if (!connection_ || closed_.load(std::memory_order_acquire)) {
            return {ProbeError::InvalidState, {}};
        }
        try {
            connection_->setRemoteDescription(
                rtc::Description(answerSdp.toStdString(), "answer")
            );
        } catch (...) {
            return {ProbeError::LibraryFailure, {}};
        }
        return waitConnected(timeout);
    }

    ProbeConnectionResult waitConnected(std::chrono::milliseconds timeout)
    {
        if (!connection_ || closed_.load(std::memory_order_acquire)) {
            return {ProbeError::InvalidState, {}};
        }
        std::unique_lock lock(state_->mutex);
        const bool signaled = state_->changed.wait_for(
            lock,
            timeout,
            [state = state_] {
                return state->connected || state->failed || state->closing;
            }
        );
        if (!signaled) return {ProbeError::ConnectionTimeout, {}};
        if (!state_->connected) return {ProbeError::ConnectionFailed, {}};
        lock.unlock();

        try {
            rtc::Candidate local;
            rtc::Candidate remote;
            QStringList types = localCandidateTypes_;
            if (connection_->getSelectedCandidatePair(&local, &remote)) {
                const QString localType = candidateTypeName(local.type());
                const QString remoteType = candidateTypeName(remote.type());
                if (!types.contains(localType)) types.push_back(localType);
                if (!types.contains(remoteType)) types.push_back(remoteType);
                std::sort(types.begin(), types.end());
            }
            return {ProbeError::None, types};
        } catch (...) {
            return {ProbeError::LibraryFailure, {}};
        }
    }

    void close() noexcept
    {
        if (closed_.exchange(true, std::memory_order_acq_rel)) return;
        {
            const std::lock_guard lock(state_->mutex);
            state_->closing = true;
            state_->changed.notify_all();
        }

        try {
            if (connection_) connection_->resetCallbacks();
        } catch (...) {
        }

        std::shared_ptr<rtc::DataChannel> channel;
        {
            const std::lock_guard lock(state_->mutex);
            channel = std::move(state_->dataChannel);
        }
        try {
            if (channel) channel->close();
        } catch (...) {
        }
        try {
            if (connection_) connection_->close();
        } catch (...) {
        }
        connection_.reset();
    }

private:
    bool initialize()
    {
        if (connection_ || closed_.load(std::memory_order_acquire)) return false;
        try {
            rtc::Configuration configuration;
            configuration.iceServers.clear();
            configuration.disableAutoNegotiation = true;
            configuration.enableIceTcp = false;
            connection_ = std::make_shared<rtc::PeerConnection>(configuration);
        } catch (...) {
            return false;
        }

        const std::weak_ptr<SharedState> weakState(state_);
        connection_->onGatheringStateChange(
            [weakState](rtc::PeerConnection::GatheringState gathering) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing) return;
                state->gatheringComplete =
                    gathering == rtc::PeerConnection::GatheringState::Complete;
                state->changed.notify_all();
            }
        );
        connection_->onStateChange(
            [weakState](rtc::PeerConnection::State value) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing) return;
                state->state = value;
                state->connected = value == rtc::PeerConnection::State::Connected;
                state->failed = value == rtc::PeerConnection::State::Failed ||
                                value == rtc::PeerConnection::State::Closed;
                state->changed.notify_all();
            }
        );
        connection_->onIceStateChange(
            [weakState](rtc::PeerConnection::IceState value) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing) return;
                state->iceState = value;
                if (value == rtc::PeerConnection::IceState::Failed ||
                    value == rtc::PeerConnection::IceState::Closed) {
                    state->failed = true;
                }
                state->changed.notify_all();
            }
        );
        connection_->onDataChannel(
            [weakState](std::shared_ptr<rtc::DataChannel> channel) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing) {
                    try {
                        channel->close();
                    } catch (...) {
                    }
                    return;
                }
                state->dataChannel = std::move(channel);
            }
        );
        return true;
    }

    ProbeDescriptionResult waitForDescription(
        std::chrono::milliseconds timeout
    )
    {
        std::unique_lock lock(state_->mutex);
        const bool signaled = state_->changed.wait_for(
            lock,
            timeout,
            [state = state_] {
                return state->gatheringComplete || state->failed ||
                       state->closing;
            }
        );
        if (!signaled) return {ProbeError::GatheringTimeout, {}, {}};
        if (!state_->gatheringComplete) {
            return {ProbeError::ConnectionFailed, {}, {}};
        }
        lock.unlock();

        try {
            const std::optional<rtc::Description> description =
                connection_->localDescription();
            if (!description.has_value()) {
                return {ProbeError::LibraryFailure, {}, {}};
            }
            localCandidateTypes_ = candidateTypes(*description);
            return {
                ProbeError::None,
                QString::fromStdString(description->generateSdp()),
                localCandidateTypes_,
            };
        } catch (...) {
            return {ProbeError::LibraryFailure, {}, {}};
        }
    }

    std::shared_ptr<SharedState> state_;
    std::shared_ptr<rtc::PeerConnection> connection_;
    QStringList localCandidateTypes_;
    std::atomic_bool closed_ {false};
};

PeerConnectionProbe::PeerConnectionProbe() : impl_(std::make_unique<Impl>()) {}

PeerConnectionProbe::~PeerConnectionProbe() = default;

ProbeDescriptionResult PeerConnectionProbe::createOffer(
    std::chrono::milliseconds timeout
)
{
    return impl_->createOffer(timeout);
}

ProbeDescriptionResult PeerConnectionProbe::createAnswer(
    const QString &offerSdp,
    std::chrono::milliseconds timeout
)
{
    return impl_->createAnswer(offerSdp, timeout);
}

ProbeConnectionResult PeerConnectionProbe::applyAnswerAndWait(
    const QString &answerSdp,
    std::chrono::milliseconds timeout
)
{
    return impl_->applyAnswerAndWait(answerSdp, timeout);
}

ProbeConnectionResult PeerConnectionProbe::waitConnected(
    std::chrono::milliseconds timeout
)
{
    return impl_->waitConnected(timeout);
}

void PeerConnectionProbe::close() noexcept
{
    impl_->close();
}

QString PeerConnectionProbe::errorName(ProbeError error)
{
    switch (error) {
    case ProbeError::None: return QStringLiteral("none");
    case ProbeError::InvalidState: return QStringLiteral("invalid_state");
    case ProbeError::LibraryFailure: return QStringLiteral("library_failure");
    case ProbeError::GatheringTimeout: return QStringLiteral("gathering_timeout");
    case ProbeError::ConnectionTimeout: return QStringLiteral("connection_timeout");
    case ProbeError::ConnectionFailed: return QStringLiteral("connection_failed");
    }
    return QStringLiteral("unknown");
}

} // namespace rtmp_monitor::webrtc_dev
