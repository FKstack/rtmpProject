#pragma once

#include "webrtc_contracts/WebRtcSessionContracts.h"
#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QString>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace rtmp_monitor::webrtc_runtime {

enum class ReceiveSessionEventKind {
    Started,
    DescriptionExported,
    Connected,
    ConnectionLost,
    Failed,
    Cancelled,
};

struct ReceiveSessionEvent
{
    ReceiveSessionEventKind kind = ReceiveSessionEventKind::Started;
    QString reason;
    rtmp_monitor::webrtc_transport::EndpointConnectionResult connection;
    rtmp_monitor::webrtc_transport::EndpointSnapshot snapshot;
};

using ReceiveSessionEventSink = std::function<void(ReceiveSessionEvent)>;

struct WebRtcReceiveSessionOptions
{
    QString exchangeRoot;
    SignalingRole signalingRole = SignalingRole::Answerer;
    IceRuntimeConfig ice;
    std::chrono::milliseconds signalingTimeout = std::chrono::seconds(30);
};

/**
 * Owns one cancellable ReceiveOnly endpoint and its managed file signaling.
 *
 * The worker owns all blocking negotiation. Library callbacks stay inside the
 * endpoint; the event sink receives only address-free value copies.
 */
class WebRtcReceiveSession final
{
public:
    WebRtcReceiveSession(
        WebRtcReceiveSessionOptions options,
        rtmp_monitor::webrtc_transport::H264ReceiveSink receiveSink,
        ReceiveSessionEventSink eventSink
    );
    ~WebRtcReceiveSession();

    WebRtcReceiveSession(const WebRtcReceiveSession &) = delete;
    WebRtcReceiveSession &operator=(const WebRtcReceiveSession &) = delete;

    [[nodiscard]] bool start();
    void requestStop() noexcept;
    void join() noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] rtmp_monitor::webrtc_transport::EndpointSnapshot
    snapshot() const noexcept;
    [[nodiscard]] std::optional<
        rtmp_monitor::webrtc_transport::EndpointConnectionResult>
    connectionResult() const;

private:
    void run() noexcept;
    void runSession();
    void emitEvent(ReceiveSessionEvent event) const;
    void emitFailure(
        const QString &reason,
        const rtmp_monitor::webrtc_transport::EndpointConnectionResult &result = {}
    ) const;
    void rememberTerminalSnapshot() noexcept;

    WebRtcReceiveSessionOptions options_;
    rtmp_monitor::webrtc_transport::H264ReceiveSink receiveSink_;
    ReceiveSessionEventSink eventSink_;
    std::atomic_bool stopRequested_ {false};
    std::atomic_bool started_ {false};
    std::atomic_bool running_ {false};
    std::thread worker_;
    mutable std::mutex resourcesMutex_;
    std::unique_ptr<rtmp_monitor::webrtc_transport::WebRtcEndpointSession>
        endpoint_;
    rtmp_monitor::webrtc_transport::EndpointSnapshot terminalSnapshot_;
    std::optional<rtmp_monitor::webrtc_transport::EndpointConnectionResult>
        connectionResult_;
};

} // namespace rtmp_monitor::webrtc_runtime
