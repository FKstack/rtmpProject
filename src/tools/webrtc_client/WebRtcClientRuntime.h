#pragma once

#include "publisher/Mp4H264PublisherSource.h"
#include "publisher/CameraH264PublisherSource.h"
#include "webrtc_client/WebRtcClientOptions.h"
#include "webrtc_client/WebRtcIceRuntimeConfigLoader.h"
#include "webrtc_client/WebRtcClientRuntimePaths.h"
#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QJsonObject>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace rtmp_monitor::webrtc_client {

using ClientEventSink =
    std::function<void(const QString &, QJsonObject)>;

struct WebRtcViewerEvidence
{
    std::atomic_bool decoded {false};
    std::atomic_bool presented {false};
};

/** Owns one cancellable signaling/endpoint/source worker session. */
class WebRtcClientRuntime final
{
public:
    WebRtcClientRuntime(
        WebRtcClientOptions options,
        ClientEventSink eventSink,
        rtmp_monitor::webrtc_transport::H264ReceiveSink receiveSink = {},
        std::shared_ptr<WebRtcViewerEvidence> viewerEvidence = {}
    );
    ~WebRtcClientRuntime();

    WebRtcClientRuntime(const WebRtcClientRuntime &) = delete;
    WebRtcClientRuntime &operator=(const WebRtcClientRuntime &) = delete;

    [[nodiscard]] bool start();
    void requestStop() noexcept;
    void join() noexcept;
    [[nodiscard]] int exitCode() const noexcept;

private:
    void run() noexcept;
    int runSession();
    int runPublisherMedia(
        rtmp_monitor::webrtc_transport::WebRtcEndpointSession &endpoint
    );
    int runViewerMedia(
        rtmp_monitor::webrtc_transport::WebRtcEndpointSession &endpoint
    );
    void emitEvent(const QString &event, QJsonObject details = {}) const;
    void emitFailure(const QString &error, QJsonObject details = {}) const;
    void emitIceGathering(
        const rtmp_monitor::webrtc_transport::EndpointDescriptionResult &result
    ) const;

    WebRtcClientOptions options_;
    WebRtcClientRuntimePathResolution runtimePaths_;
    ClientEventSink eventSink_;
    rtmp_monitor::webrtc_transport::H264ReceiveSink receiveSink_;
    std::shared_ptr<WebRtcViewerEvidence> viewerEvidence_;
    std::atomic_bool stopRequested_ {false};
    std::atomic_bool started_ {false};
    std::atomic_int exitCode_ {4};
    std::thread worker_;
    mutable std::mutex resourcesMutex_;
    std::unique_ptr<rtmp_monitor::webrtc_transport::WebRtcEndpointSession>
        endpoint_;
    std::unique_ptr<rtmp_monitor::publisher::Mp4H264PublisherSource> source_;
    std::unique_ptr<rtmp_monitor::publisher::CameraH264PublisherSource>
        cameraSource_;
};

} // namespace rtmp_monitor::webrtc_client
