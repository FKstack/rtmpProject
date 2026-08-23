#include <rtc/rtc.hpp>

#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
namespace {
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
constexpr auto kRunTimeout = std::chrono::seconds(20);
constexpr auto kCleanupTimeout = std::chrono::seconds(10);
constexpr std::string_view kLabel = "minilab";
enum class Error {
    None,
    InvalidState,
    LibraryFailure,
    Timeout,
    ConnectionFailed,
    ProtocolMismatch,
    CleanupTimeout,
};
const char *errorName(Error error) noexcept
{
    switch (error) {
    case Error::None: return "none";
    case Error::InvalidState: return "invalid_state";
    case Error::LibraryFailure: return "library_failure";
    case Error::Timeout: return "timeout";
    case Error::ConnectionFailed: return "connection_failed";
    case Error::ProtocolMismatch: return "protocol_mismatch";
    case Error::CleanupTimeout: return "cleanup_timeout";
    }
    return "library_failure";
}
struct SharedState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool closing = false;
    std::uint64_t generation = 1;
    bool gathered[2] {false, false};
    bool connected[2] {false, false};
    bool channelOpen[2] {false, false};
    bool pingReceived = false;
    bool pongReceived = false;
    bool failed = false;
    Error error = Error::None;
    std::shared_ptr<rtc::DataChannel> channels[2];
};
void fail(
    const std::weak_ptr<SharedState> &weakState,
    std::uint64_t generation,
    Error error
) noexcept
{
    const auto state = weakState.lock();
    if (!state) return;
    const std::lock_guard lock(state->mutex);
    if (state->closing || state->generation != generation) return;
    state->failed = true;
    state->error = error;
    state->changed.notify_all();
}
void attachChannel(
    const std::weak_ptr<SharedState> &weakState,
    std::uint64_t generation,
    int side,
    std::shared_ptr<rtc::DataChannel> channel
)
{
    const auto state = weakState.lock();
    if (!state) return;
    try {
        if (channel->label() != std::string(kLabel)) {
            fail(weakState, generation, Error::ProtocolMismatch);
            channel->close();
            return;
        }
        channel->onOpen([weakState, generation, side] {
            const auto state = weakState.lock();
            if (!state) return;
            const std::lock_guard lock(state->mutex);
            if (state->closing || state->generation != generation) return;
            state->channelOpen[side] = true;
            state->changed.notify_all();
        });
        channel->onMessage(
            [weakState, generation](rtc::binary) {
                fail(weakState, generation, Error::ProtocolMismatch);
            },
            [weakState, generation, side](rtc::string message) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing || state->generation != generation) return;
                const bool expected = side == 0 ? message == "pong"
                                                : message == "ping";
                if (!expected) {
                    state->failed = true;
                    state->error = Error::ProtocolMismatch;
                } else if (side == 0) {
                    state->pongReceived = true;
                } else {
                    state->pingReceived = true;
                }
                state->changed.notify_all();
            }
        );
        channel->onError([weakState, generation](rtc::string) {
            fail(weakState, generation, Error::ConnectionFailed);
        });

        const bool alreadyOpen = channel->isOpen();
        bool reject = false;
        if (state) {
            const std::lock_guard lock(state->mutex);
            reject = state->closing || state->generation != generation;
            if (!reject) {
                state->channels[side] = channel;
                state->channelOpen[side] = alreadyOpen;
                state->changed.notify_all();
            }
        } else reject = true;
        if (reject) {
            channel->resetCallbacks();
            channel->close();
        }
    } catch (...) {
        fail(weakState, generation, Error::LibraryFailure);
    }
}
struct RunResult
{
    Error error = Error::None;
    const char *stage = "complete";
    std::chrono::milliseconds elapsed {0};
};
class MiniLabSession final
{
public:
    MiniLabSession() : state_(std::make_shared<SharedState>()) {}
    ~MiniLabSession() { (void)close(); }
    MiniLabSession(const MiniLabSession &) = delete;
    MiniLabSession &operator=(const MiniLabSession &) = delete;
    RunResult run(int round)
    {
        const auto started = Clock::now();
        const Deadline deadline = started + kRunTimeout;
        std::cout << "round=" << round << " stage=start result=success\n";
        try {
            rtc::Configuration configuration;
            configuration.iceServers.clear();
            configuration.disableAutoNegotiation = true;
            configuration.enableIceTcp = false;
            peers_[0] = std::make_shared<rtc::PeerConnection>(configuration);
            peers_[1] = std::make_shared<rtc::PeerConnection>(configuration);
            registerPeer(0);
            registerPeer(1);

            const std::weak_ptr<SharedState> weakState(state_);
            const std::uint64_t generation = state_->generation;
            peers_[1]->onDataChannel(
                [weakState, generation](std::shared_ptr<rtc::DataChannel> channel) {
                    attachChannel(weakState, generation, 1, std::move(channel));
                }
            );
            attachChannel(
                weakState,
                generation,
                0,
                peers_[0]->createDataChannel(std::string(kLabel))
            );
            std::cout << "round=" << round
                      << " stage=peers state=new result=success\n";
            peers_[0]->setLocalDescription(rtc::Description::Type::Offer);
            if (const Error error = waitUntil(
                    deadline, [](const SharedState &s) { return s.gathered[0]; }
                ); error != Error::None) {
                return failure(started, "offer_gathering", error);
            }
            const auto offer = peers_[0]->localDescription();
            if (!offer) return failure(started, "offer", Error::InvalidState);
            peers_[1]->setRemoteDescription(
                rtc::Description(offer->generateSdp(), "offer")
            );
            peers_[1]->setLocalDescription(rtc::Description::Type::Answer);
            std::cout << "round=" << round
                      << " stage=offer state=complete result=success\n";
            if (const Error error = waitUntil(
                    deadline, [](const SharedState &s) { return s.gathered[1]; }
                ); error != Error::None) {
                return failure(started, "answer_gathering", error);
            }
            const auto answer = peers_[1]->localDescription();
            if (!answer) return failure(started, "answer", Error::InvalidState);
            peers_[0]->setRemoteDescription(
                rtc::Description(answer->generateSdp(), "answer")
            );
            std::cout << "round=" << round
                      << " stage=answer state=complete result=success\n";
            if (const Error error = waitUntil(deadline, [](const SharedState &s) {
                    return s.connected[0] && s.connected[1];
                }); error != Error::None) {
                return failure(started, "connection", error);
            }
            if (const Error error = waitUntil(deadline, [](const SharedState &s) {
                    return s.channelOpen[0] && s.channelOpen[1] &&
                           s.channels[0] && s.channels[1];
                }); error != Error::None) {
                return failure(started, "data_channel", error);
            }
            std::cout << "round=" << round
                      << " stage=connection state=connected result=success\n";
            std::cout << "round=" << round
                      << " stage=data_channel state=open result=success\n";
            const auto pingStarted = Clock::now();
            (void)channel(0)->send(std::string("ping"));
            if (const Error error = waitUntil(
                    deadline, [](const SharedState &s) { return s.pingReceived; }
                ); error != Error::None) {
                return failure(started, "ping", error);
            }
            (void)channel(1)->send(std::string("pong"));
            if (const Error error = waitUntil(
                    deadline, [](const SharedState &s) { return s.pongReceived; }
                ); error != Error::None) {
                return failure(started, "pong", error);
            }
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(Clock::now() - pingStarted);
            std::cout << "round=" << round
                      << " stage=ping_pong elapsed_ms=" << elapsed.count()
                      << " result=success\n";
            return {Error::None, "complete", elapsed};
        } catch (...) {
            return failure(started, "runtime", Error::LibraryFailure);
        }
    }

    bool close() noexcept
    {
        if (closed_) return closeSucceeded_;
        closed_ = true;
        std::shared_ptr<rtc::DataChannel> channels[2];
        {
            const std::lock_guard lock(state_->mutex);
            state_->closing = true;
            ++state_->generation;
            state_->changed.notify_all();
            channels[0] = std::move(state_->channels[0]);
            channels[1] = std::move(state_->channels[1]);
        }
        for (auto &channel : channels) resetCallbacks(channel);
        for (auto &peer : peers_) resetCallbacks(peer);
        for (auto &channel : channels) closeObject(channel);
        for (auto &peer : peers_) closeObject(peer);
        peers_[0].reset();
        peers_[1].reset();
        return closeSucceeded_;
    }

private:
    void registerPeer(int side)
    {
        const std::weak_ptr<SharedState> weakState(state_);
        const std::uint64_t generation = state_->generation;
        peers_[side]->onGatheringStateChange(
            [weakState, generation, side](
                rtc::PeerConnection::GatheringState value
            ) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing || state->generation != generation) return;
                if (value == rtc::PeerConnection::GatheringState::Complete) {
                    state->gathered[side] = true;
                }
                state->changed.notify_all();
            }
        );
        peers_[side]->onStateChange(
            [weakState, generation, side](rtc::PeerConnection::State value) {
                const auto state = weakState.lock();
                if (!state) return;
                const std::lock_guard lock(state->mutex);
                if (state->closing || state->generation != generation) return;
                if (value == rtc::PeerConnection::State::Connected) {
                    state->connected[side] = true;
                } else if (value == rtc::PeerConnection::State::Failed ||
                           value == rtc::PeerConnection::State::Closed) {
                    state->failed = true;
                    state->error = Error::ConnectionFailed;
                }
                state->changed.notify_all();
            }
        );
    }
    template<typename Predicate>
    Error waitUntil(Deadline deadline, Predicate ready)
    {
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_until(lock, deadline, [&] {
                return ready(*state_) || state_->failed || state_->closing;
            })) {
            return Error::Timeout;
        }
        if (state_->failed) return state_->error;
        return state_->closing ? Error::InvalidState : Error::None;
    }
    std::shared_ptr<rtc::DataChannel> channel(int side)
    {
        const std::lock_guard lock(state_->mutex);
        return state_->channels[side];
    }
    static RunResult failure(
        Clock::time_point started,
        const char *stage,
        Error error
    ) noexcept
    {
        return {
            error,
            stage,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - started
            ),
        };
    }
    template<typename T>
    void resetCallbacks(const std::shared_ptr<T> &object) noexcept
    {
        if (!object) return;
        try { object->resetCallbacks(); }
        catch (...) { closeSucceeded_ = false; }
    }
    template<typename T>
    void closeObject(const std::shared_ptr<T> &object) noexcept
    {
        if (!object) return;
        try { object->close(); }
        catch (...) { closeSucceeded_ = false; }
    }
    std::shared_ptr<SharedState> state_;
    std::shared_ptr<rtc::PeerConnection> peers_[2];
    bool closed_ = false;
    bool closeSucceeded_ = true;
};
struct Arguments { int repeat = 1; bool help = false; bool valid = true; };
Arguments parseArguments(int argc, char *argv[]) noexcept
{
    Arguments result;
    if (argc == 1) return result;
    if (argc != 2) {
        result.valid = false;
        return result;
    }
    const std::string_view argument(argv[1]);
    if (argument == "--help") {
        result.help = true;
        return result;
    }
    constexpr std::string_view prefix = "--repeat=";
    if (argument.substr(0, prefix.size()) != prefix) {
        result.valid = false;
        return result;
    }
    const std::string_view number = argument.substr(prefix.size());
    const auto parsed = std::from_chars(
        number.data(), number.data() + number.size(), result.repeat
    );
    result.valid = !number.empty() && parsed.ec == std::errc() &&
                   parsed.ptr == number.data() + number.size() &&
                   result.repeat >= 1 && result.repeat <= 100;
    return result;
}
void printUsage()
{
    std::cout << "usage: webrtc_minilab [--repeat=N]\n"
              << "       webrtc_minilab --help\n"
              << "repeat_range: 1..100\n";
}
} // namespace
int main(int argc, char *argv[])
{
    const Arguments arguments = parseArguments(argc, argv);
    if (!arguments.valid) {
        std::cout << "result=failure error=invalid_arguments\n";
        printUsage();
        return 2;
    }
    if (arguments.help) {
        printUsage();
        return 0;
    }
    bool succeeded = true;
    int completed = 0;
    try {
        rtc::InitLogger(rtc::LogLevel::None);
        for (int round = 1; round <= arguments.repeat; ++round) {
            MiniLabSession session;
            RunResult result = session.run(round);
            const bool firstClose = session.close();
            const bool secondClose = session.close();
            if (!firstClose || !secondClose) {
                result = {Error::LibraryFailure, "shutdown", result.elapsed};
            }
            std::cout << "round=" << round
                      << " stage=shutdown state=closed result="
                      << ((firstClose && secondClose) ? "success" : "failure")
                      << '\n';
            if (result.error != Error::None) {
                std::cout << "round=" << round
                          << " stage=" << result.stage
                          << " elapsed_ms=" << result.elapsed.count()
                          << " result=failure error=" << errorName(result.error)
                          << '\n';
                succeeded = false;
                break;
            }
            ++completed;
        }
    } catch (...) {
        std::cout << "stage=runtime result=failure error=library_failure\n";
        succeeded = false;
    }
    try {
        auto cleanup = rtc::Cleanup();
        if (cleanup.wait_for(kCleanupTimeout) == std::future_status::timeout) {
            std::cout << "stage=cleanup result=failure error="
                      << errorName(Error::CleanupTimeout) << '\n';
            succeeded = false;
        } else {
            cleanup.get();
            std::cout << "stage=cleanup state=complete result=success\n";
        }
    } catch (...) {
        std::cout << "stage=cleanup result=failure error="
                  << errorName(Error::LibraryFailure) << '\n';
        succeeded = false;
    }
    std::cout << "summary rounds=" << completed
              << " result=" << (succeeded ? "success" : "failure") << '\n';
    return succeeded && completed == arguments.repeat ? 0 : 1;
}
