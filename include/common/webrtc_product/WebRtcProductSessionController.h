#pragma once

#include "webrtc_product/WebRtcProductTypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

#include <chrono>
#include <memory>
#include <optional>

class QAction;
class EncodedVideoInputHandle;
class LatestFrameMailbox;
class LogManager;
class MainWindow;
class MultiStreamPlaybackManager;
class QMenu;
class QTimer;
class VideoWidget;

namespace rtmp_monitor::webrtc_runtime {
class WebRtcReceiveSession;
struct ReceiveSessionEvent;
}

namespace rtmp_monitor::webrtc_product {

/**
 * Application-layer owner for the single Week 8 receive session.
 *
 * This controller is the only object allowed to assemble transport, the
 * external media ingress and a product video widget. It is constructed only
 * in WebRTC-enabled builds and never registers a device/control binding.
 */
class WebRtcProductSessionController final : public QObject
{
    Q_OBJECT

public:
    WebRtcProductSessionController(
        MainWindow *mainWindow,
        MultiStreamPlaybackManager *playbackManager,
        LogManager *logManager,
        QString exchangeRootOverride = {},
        QObject *parent = nullptr
    );
    ~WebRtcProductSessionController() override;

    WebRtcProductSessionController(
        const WebRtcProductSessionController &
    ) = delete;
    WebRtcProductSessionController &operator=(
        const WebRtcProductSessionController &
    ) = delete;

    [[nodiscard]] bool start(
        WebRtcSessionRequest request,
        QString *error = nullptr
    );
    void cancel();
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] WebRtcProductState state() const noexcept;
    [[nodiscard]] WebRtcProductDiagnostics diagnosticsSnapshot() const;
    [[nodiscard]] QString exchangeRoot() const;

    /** Must run after all product sessions have been stopped. */
    [[nodiscard]] static bool cleanupGlobal(
        std::chrono::milliseconds timeout = std::chrono::seconds(10)
    ) noexcept;

signals:
    void stateChanged(rtmp_monitor::webrtc_product::WebRtcProductState state);
    void eventObserved(
        const rtmp_monitor::webrtc_product::WebRtcProductEvent &event
    );
    void diagnosticsChanged(
        const rtmp_monitor::webrtc_product::WebRtcProductDiagnostics &snapshot
    );

private:
    void showStartDialog();
    void handleSessionEvent(
        std::uint64_t token,
        rtmp_monitor::webrtc_runtime::ReceiveSessionEvent event
    );
    void pollDiagnostics();
    void setState(WebRtcProductState state, const QString &statusText);
    void publishEvent(WebRtcProductEventKind kind, const QString &reason = {});
    void logEvent(
        const QString &eventName,
        const QString &message,
        const QString &reason = {}
    );
    void releaseSessionObjects(bool removeWidget);
    [[nodiscard]] QString defaultExchangeRoot() const;

    MainWindow *mainWindow_ = nullptr;
    MultiStreamPlaybackManager *playbackManager_ = nullptr;
    LogManager *logManager_ = nullptr;
    QString exchangeRootOverride_;
    QMenu *menu_ = nullptr;
    QAction *startAction_ = nullptr;
    QAction *cancelAction_ = nullptr;
    QTimer *diagnosticsTimer_ = nullptr;
    QPointer<VideoWidget> videoWidget_;
    std::shared_ptr<EncodedVideoInputHandle> inputHandle_;
    std::unique_ptr<rtmp_monitor::webrtc_runtime::WebRtcReceiveSession>
        session_;
    std::shared_ptr<LatestFrameMailbox> mailbox_;
    std::optional<rtmp_monitor::webrtc_transport::EndpointConnectionResult>
        connectionResult_;
    WebRtcProductState state_ = WebRtcProductState::Idle;
    QElapsedTimer connectedTimer_;
    std::uint64_t sessionToken_ = 0;
    bool directObserved_ = false;
};

} // namespace rtmp_monitor::webrtc_product

Q_DECLARE_METATYPE(rtmp_monitor::webrtc_product::WebRtcProductState)
Q_DECLARE_METATYPE(rtmp_monitor::webrtc_product::WebRtcProductEvent)
Q_DECLARE_METATYPE(rtmp_monitor::webrtc_product::WebRtcProductDiagnostics)
