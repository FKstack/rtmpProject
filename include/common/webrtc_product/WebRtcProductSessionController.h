#pragma once

#include "webrtc_product/WebRtcProductTypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

#include <chrono>
#include <memory>
#include <map>
#include <vector>

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
 * Application-layer owner for up to four isolated runtime receive sessions.
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
        QString *error = nullptr,
        StreamId *createdStreamId = nullptr
    );
    void cancel();
    void cancel(StreamId streamId);
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] bool isActive(StreamId streamId) const noexcept;
    [[nodiscard]] std::vector<StreamId> activeStreamIds() const;
    [[nodiscard]] WebRtcProductState state() const noexcept;
    [[nodiscard]] WebRtcProductState state(StreamId streamId) const noexcept;
    [[nodiscard]] WebRtcProductDiagnostics diagnosticsSnapshot() const;
    [[nodiscard]] WebRtcProductDiagnostics diagnosticsSnapshot(
        StreamId streamId
    ) const;
    [[nodiscard]] std::vector<WebRtcProductDiagnostics>
        diagnosticsSnapshots() const;
    [[nodiscard]] QString exchangeRoot() const;
    [[nodiscard]] QString exchangeRoot(StreamId streamId) const;

    /** Must run after all product sessions have been stopped. */
    [[nodiscard]] static bool cleanupGlobal(
        std::chrono::milliseconds timeout = std::chrono::seconds(10)
    ) noexcept;

signals:
    void stateChanged(rtmp_monitor::webrtc_product::WebRtcProductState state);
    void streamStateChanged(
        StreamId streamId,
        rtmp_monitor::webrtc_product::WebRtcProductState state
    );
    void eventObserved(
        const rtmp_monitor::webrtc_product::WebRtcProductEvent &event
    );
    void diagnosticsChanged(
        const rtmp_monitor::webrtc_product::WebRtcProductDiagnostics &snapshot
    );

private:
    struct SessionContext;
    void showStartDialog();
    void handleSessionEvent(
        StreamId streamId,
        std::uint64_t token,
        rtmp_monitor::webrtc_runtime::ReceiveSessionEvent event
    );
    void pollDiagnostics();
    void setState(
        StreamId streamId,
        WebRtcProductState state,
        const QString &statusText
    );
    void publishEvent(
        StreamId streamId,
        WebRtcProductEventKind kind,
        const QString &reason = {}
    );
    void logEvent(
        StreamId streamId,
        const QString &eventName,
        const QString &message,
        const QString &reason = {}
    );
    void releaseSessionObjects(StreamId streamId, bool removeWidget);
    void releaseDetachedSession(
        StreamId streamId,
        std::unique_ptr<SessionContext> context,
        bool removeWidget
    );
    [[nodiscard]] std::unique_ptr<SessionContext> detachSession(
        StreamId streamId
    );
    void removeWidgetOrRetry(QPointer<VideoWidget> widget);
    void retryPendingWidgetRemovals();
    void updateActionsAndTimer();
    [[nodiscard]] int lowestFreeSlot() const noexcept;
    [[nodiscard]] QString defaultExchangeRoot() const;

    MainWindow *mainWindow_ = nullptr;
    MultiStreamPlaybackManager *playbackManager_ = nullptr;
    LogManager *logManager_ = nullptr;
    QString exchangeRootOverride_;
    QMenu *menu_ = nullptr;
    QAction *startAction_ = nullptr;
    QAction *cancelAction_ = nullptr;
    QTimer *diagnosticsTimer_ = nullptr;
    std::map<StreamId, std::unique_ptr<SessionContext>> sessions_;
    std::vector<QPointer<VideoWidget>> pendingWidgetRemovals_;
    std::uint64_t nextSessionToken_ = 0;
    WebRtcProductState lastAggregateState_ = WebRtcProductState::Idle;
    bool closingAll_ = false;
    bool widgetRemovalRetryScheduled_ = false;
};

} // namespace rtmp_monitor::webrtc_product

Q_DECLARE_METATYPE(rtmp_monitor::webrtc_product::WebRtcProductState)
Q_DECLARE_METATYPE(rtmp_monitor::webrtc_product::WebRtcProductEvent)
Q_DECLARE_METATYPE(rtmp_monitor::webrtc_product::WebRtcProductDiagnostics)
