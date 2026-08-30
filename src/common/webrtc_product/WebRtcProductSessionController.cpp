#include "webrtc_product/WebRtcProductSessionController.h"

#include "logging/LogManager.h"
#include "media/EncodedVideoInputHandle.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"
#include "webrtc_runtime/WebRtcReceiveSession.h"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <future>
#include <array>
#include <optional>
#include <utility>

namespace rtmp_monitor::webrtc_product {
namespace {

constexpr qint64 kPresentationFreshnessMs = 1'000;
constexpr qint64 kInitialMediaTimeoutMs = 10'000;

class WebRtcSessionDialog final : public QDialog
{
public:
    WebRtcSessionDialog(QString exchangeRoot, QWidget *parent)
        : QDialog(parent), exchangeRoot_(std::move(exchangeRoot))
    {
        setObjectName(QStringLiteral("oneShotWebRtcDialog"));
        setWindowTitle(tr("一次性 WebRTC 接收"));
        setModal(true);
        resize(620, 280);

        auto *layout = new QFormLayout(this);
        auto *explanation = new QLabel(
            tr("此入口只创建当前运行期的接收会话，不保存设备、Peer、SDP、"
               "STUN 或自动连接配置，也不会授权设备控制。"),
            this
        );
        explanation->setWordWrap(true);
        layout->addRow(explanation);

        displayName_ = new QLineEdit(
            tr("WebRTC 临时画面"), this
        );
        displayName_->setObjectName(QStringLiteral("webrtcDisplayName"));
        displayName_->setMaxLength(64);
        layout->addRow(tr("显示名称"), displayName_);

        role_ = new QComboBox(this);
        role_->setObjectName(QStringLiteral("webrtcSignalingRole"));
        role_->addItem(tr("等待发布端 Offer（接收端作为 Answerer）"));
        role_->addItem(tr("由接收端生成 Offer（接收端作为 Offerer）"));
        layout->addRow(tr("信令角色"), role_);

        iceMode_ = new QComboBox(this);
        iceMode_->setObjectName(QStringLiteral("webrtcIceMode"));
        iceMode_->addItem(tr("Host（默认，不访问 STUN）"));
        iceMode_->addItem(tr("STUN（仅本次，在下面明确输入）"));
        layout->addRow(tr("ICE 模式"), iceMode_);

        stunUrl_ = new QLineEdit(this);
        stunUrl_->setObjectName(QStringLiteral("webrtcStunUrl"));
        stunUrl_->setPlaceholderText(QStringLiteral("stun:<stun-host>:3478"));
        stunUrl_->setEnabled(false);
        layout->addRow(tr("STUN URL"), stunUrl_);
        connect(
            iceMode_, &QComboBox::currentIndexChanged,
            stunUrl_, [this](int index) { stunUrl_->setEnabled(index == 1); }
        );

        auto *pathRow = new QWidget(this);
        auto *pathLayout = new QHBoxLayout(pathRow);
        pathLayout->setContentsMargins(0, 0, 0, 0);
        auto *path = new QLineEdit(exchangeRoot_, pathRow);
        path->setObjectName(QStringLiteral("webrtcExchangeRoot"));
        path->setReadOnly(true);
        auto *open = new QPushButton(tr("打开目录"), pathRow);
        pathLayout->addWidget(path, 1);
        pathLayout->addWidget(open);
        layout->addRow(tr("受管交换目录"), pathRow);
        connect(open, &QPushButton::clicked, this, [this] {
            if (QDir().mkpath(exchangeRoot_)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(exchangeRoot_));
            }
        });

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this
        );
        buttons->button(QDialogButtonBox::Ok)->setText(tr("开始接收"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addRow(buttons);
    }

    [[nodiscard]] WebRtcSessionRequest request() const
    {
        WebRtcSessionRequest result;
        result.displayName = displayName_->text().trimmed();
        result.signalingRole = role_->currentIndex() == 0
                                   ? SignalingRole::Answerer
                                   : SignalingRole::Offerer;
        if (iceMode_->currentIndex() == 1) {
            IceServerRuntimeConfig server;
            server.urls.push_back(stunUrl_->text().trimmed().toStdString());
            result.ice.servers.push_back(std::move(server));
        }
        return result;
    }

private:
    QString exchangeRoot_;
    QLineEdit *displayName_ = nullptr;
    QComboBox *role_ = nullptr;
    QComboBox *iceMode_ = nullptr;
    QLineEdit *stunUrl_ = nullptr;
};

QString statusForFailure(WebRtcProductState state)
{
    return state == WebRtcProductState::NeedsRelay
               ? QObject::tr("当前网络检查已失败，可能需要 Relay")
               : QObject::tr("WebRTC 会话失败，请检查交换文件、编码或网络");
}

} // namespace

WebRtcProductSessionController::WebRtcProductSessionController(
    MainWindow *mainWindow,
    MultiStreamPlaybackManager *playbackManager,
    LogManager *logManager,
    QString exchangeRootOverride,
    QObject *parent
)
    : QObject(parent),
      mainWindow_(mainWindow),
      playbackManager_(playbackManager),
      logManager_(logManager),
      exchangeRootOverride_(std::move(exchangeRootOverride))
{
    Q_ASSERT(mainWindow_ != nullptr);
    Q_ASSERT(playbackManager_ != nullptr);
    qRegisterMetaType<WebRtcProductState>();
    qRegisterMetaType<WebRtcProductEvent>();
    qRegisterMetaType<WebRtcProductDiagnostics>();

    menu_ = mainWindow_->menuBar()->addMenu(tr("WebRTC"));
    menu_->setObjectName(QStringLiteral("webrtcMenu"));
    startAction_ = menu_->addAction(tr("一次性接收..."));
    startAction_->setObjectName(QStringLiteral("oneShotWebRtcAction"));
    cancelAction_ = menu_->addAction(tr("取消全部 WebRTC 会话"));
    cancelAction_->setObjectName(QStringLiteral("cancelOneShotWebRtcAction"));
    cancelAction_->setEnabled(false);
    connect(startAction_, &QAction::triggered,
            this, &WebRtcProductSessionController::showStartDialog);
    connect(cancelAction_, &QAction::triggered,
            this, [this] { cancel(); });

    diagnosticsTimer_ = new QTimer(this);
    diagnosticsTimer_->setInterval(100);
    connect(diagnosticsTimer_, &QTimer::timeout,
            this, &WebRtcProductSessionController::pollDiagnostics);
}

WebRtcProductSessionController::~WebRtcProductSessionController()
{
    cancel();
}

struct WebRtcProductSessionController::SessionContext
{
    int slot = 0;
    std::uint64_t token = 0;
    QPointer<VideoWidget> videoWidget;
    std::shared_ptr<EncodedVideoInputHandle> inputHandle;
    std::unique_ptr<rtmp_monitor::webrtc_runtime::WebRtcReceiveSession> session;
    std::shared_ptr<LatestFrameMailbox> mailbox;
    std::optional<rtmp_monitor::webrtc_transport::EndpointConnectionResult>
        connectionResult;
    WebRtcProductState state = WebRtcProductState::Idle;
    QElapsedTimer connectedTimer;
    bool directObserved = false;
};

bool WebRtcProductSessionController::start(
    WebRtcSessionRequest request,
    QString *error,
    StreamId *createdStreamId
)
{
    if (createdStreamId) *createdStreamId = kInvalidStreamId;
    if (closingAll_) {
        if (error) *error = QStringLiteral("closing_all");
        return false;
    }
    const int slot = lowestFreeSlot();
    if (slot == 0) {
        if (error) *error = QStringLiteral("capacity_reached");
        return false;
    }
    if (!WebRtcProductPolicy::validateRequest(request, error)) return false;

    EncodedVideoInputHandle input =
        playbackManager_->createEncodedVideoInput(request.displayName.trimmed());
    if (!input.isOpen()) {
        if (error) *error = tr("无法创建接收解码入口；请检查 16 路容量。");
        return false;
    }
    auto context = std::make_unique<SessionContext>();
    context->slot = slot;
    context->token = ++nextSessionToken_;
    context->inputHandle =
        std::make_shared<EncodedVideoInputHandle>(std::move(input));
    const StreamId streamId = context->inputHandle->streamId();
    context->mailbox = playbackManager_->frameMailbox(streamId);
    context->videoWidget =
        mainWindow_->addConnectionWidget(request.displayName.trimmed());
    if (!context->videoWidget || !context->mailbox) {
        if (context->inputHandle) context->inputHandle->close();
        (void)playbackManager_->removeStream(streamId);
        if (context->videoWidget) {
            removeWidgetOrRetry(context->videoWidget);
        }
        if (error) *error = tr("无法创建 WebRTC 视频显示格。");
        return false;
    }
    mainWindow_->bindVideoStream(
        context->videoWidget, streamId, context->mailbox
    );
    // Keep the render item active while the status overlay is visible.  Direct
    // is still withheld until the mailbox reports an actually presented frame.
    context->videoWidget->showFrame();
    context->videoWidget->setAudioPlaybackState(
        AudioPlaybackState::Unavailable, false
    );
    context->videoWidget->setControlTargetSelected(false);
    connect(context->videoWidget, &VideoWidget::removeRequested,
            this, [this, streamId](VideoWidget *) { cancel(streamId); });

    const std::weak_ptr<EncodedVideoInputHandle> weakInput(
        context->inputHandle
    );
    rtmp_monitor::webrtc_runtime::WebRtcReceiveSessionOptions options;
    options.exchangeRoot = QDir(exchangeRoot()).filePath(
        QStringLiteral("session-%1").arg(slot, 2, 10, QLatin1Char('0'))
    );
    options.signalingRole = request.signalingRole;
    options.ice = std::move(request.ice);

    const std::uint64_t token = context->token;
    QPointer<WebRtcProductSessionController> target(this);
    context->session = std::make_unique<
        rtmp_monitor::webrtc_runtime::WebRtcReceiveSession>(
        std::move(options),
        [weakInput](SessionMediaSample sample) {
            const auto inputHandle = weakInput.lock();
            return inputHandle
                       ? inputHandle->submit(std::move(sample.accessUnit))
                       : H264SubmitResult::Closed;
        },
        [target, streamId, token](
            rtmp_monitor::webrtc_runtime::ReceiveSessionEvent event
        ) mutable {
            if (!target) return;
            QMetaObject::invokeMethod(
                target,
                [target, streamId, token, event = std::move(event)]() mutable {
                    if (target) {
                        target->handleSessionEvent(
                            streamId, token, std::move(event)
                        );
                    }
                },
                Qt::QueuedConnection
            );
        }
    );
    if (!context->session->start()) {
        if (context->inputHandle) context->inputHandle->close();
        (void)playbackManager_->removeStream(streamId);
        removeWidgetOrRetry(context->videoWidget);
        if (error) *error = tr("一次性 WebRTC worker 无法启动。");
        return false;
    }
    sessions_.emplace(streamId, std::move(context));
    if (createdStreamId) *createdStreamId = streamId;
    if (error) error->clear();
    updateActionsAndTimer();
    setState(streamId, WebRtcProductState::Connecting,
             tr("正在准备受管 Offer/Answer 交换..."));
    auto current = sessions_.find(streamId);
    if (current == sessions_.end() || current->second->token != token) {
        return true;
    }
    publishEvent(streamId, WebRtcProductEventKind::SessionStarted);
    current = sessions_.find(streamId);
    if (current == sessions_.end() || current->second->token != token) {
        return true;
    }
    logEvent(streamId, QStringLiteral("session_started"),
             QStringLiteral("One-shot WebRTC receive session started."));
    return true;
}

void WebRtcProductSessionController::cancel()
{
    if (closingAll_ || sessions_.empty()) return;
    closingAll_ = true;
    std::vector<std::pair<StreamId, std::unique_ptr<SessionContext>>> detached;
    detached.reserve(sessions_.size());
    while (!sessions_.empty()) {
        auto found = sessions_.begin();
        const StreamId streamId = found->first;
        auto context = std::move(found->second);
        sessions_.erase(found);
        context->token = ++nextSessionToken_;
        if (context->session) context->session->requestStop();
        detached.emplace_back(streamId, std::move(context));
    }
    updateActionsAndTimer();
    for (auto &[unused, context] : detached) {
        Q_UNUSED(unused);
        if (context->session) context->session->join();
    }
    for (auto &[streamId, context] : detached) {
        releaseDetachedSession(streamId, std::move(context), true);
        publishEvent(streamId, WebRtcProductEventKind::Cancelled);
        logEvent(streamId, QStringLiteral("session_cancelled"),
                 QStringLiteral("One-shot WebRTC receive session cancelled."));
    }
    closingAll_ = false;
    updateActionsAndTimer();
}

void WebRtcProductSessionController::cancel(StreamId streamId)
{
    if (closingAll_) return;
    std::unique_ptr<SessionContext> context = detachSession(streamId);
    if (!context) return;
    context->token = ++nextSessionToken_;
    if (context->session) {
        context->session->requestStop();
        context->session->join();
    }
    releaseDetachedSession(streamId, std::move(context), true);
    publishEvent(streamId, WebRtcProductEventKind::Cancelled);
    logEvent(streamId, QStringLiteral("session_cancelled"),
             QStringLiteral("One-shot WebRTC receive session cancelled."));
    updateActionsAndTimer();
}

bool WebRtcProductSessionController::isActive() const noexcept
{
    return !sessions_.empty();
}

bool WebRtcProductSessionController::isActive(StreamId streamId) const noexcept
{
    return sessions_.find(streamId) != sessions_.end();
}

std::vector<StreamId>
WebRtcProductSessionController::activeStreamIds() const
{
    std::vector<StreamId> result;
    result.reserve(sessions_.size());
    for (const auto &[streamId, unused] : sessions_) {
        Q_UNUSED(unused);
        result.push_back(streamId);
    }
    return result;
}

WebRtcProductState WebRtcProductSessionController::state() const noexcept
{
    bool connecting = false;
    bool direct = false;
    bool relay = false;
    for (const auto &[unused, context] : sessions_) {
        Q_UNUSED(unused);
        if (context->state == WebRtcProductState::Error) {
            return WebRtcProductState::Error;
        }
        relay = relay || context->state == WebRtcProductState::NeedsRelay;
        connecting = connecting ||
            context->state == WebRtcProductState::Connecting;
        direct = direct || context->state == WebRtcProductState::Direct;
    }
    if (relay) return WebRtcProductState::NeedsRelay;
    if (connecting) return WebRtcProductState::Connecting;
    if (direct) return WebRtcProductState::Direct;
    return WebRtcProductState::Idle;
}

WebRtcProductState WebRtcProductSessionController::state(
    StreamId streamId
) const noexcept
{
    const auto found = sessions_.find(streamId);
    return found != sessions_.end() ? found->second->state
                                    : WebRtcProductState::Idle;
}

WebRtcProductDiagnostics
WebRtcProductSessionController::diagnosticsSnapshot() const
{
    if (sessions_.size() == 1U) {
        return diagnosticsSnapshot(sessions_.begin()->first);
    }
    WebRtcProductDiagnostics result;
    result.state = state();
    return result;
}

WebRtcProductDiagnostics WebRtcProductSessionController::diagnosticsSnapshot(
    StreamId streamId
) const
{
    WebRtcProductDiagnostics result;
    const auto found = sessions_.find(streamId);
    if (found == sessions_.end()) return result;
    const SessionContext &context = *found->second;
    result.streamId = streamId;
    result.state = context.state;
    if (context.session) result.transport = context.session->snapshot();
    if (context.inputHandle) {
        result.mediaGeneration = context.inputHandle->generation();
        result.media = playbackManager_->streamMetrics(streamId);
    }
    result.presentedFrameAgeMs = context.mailbox
        ? context.mailbox->lastPresentedFrameAgeMs() : -1;
    result.selectedNonRelayPair = context.connectionResult.has_value() &&
        WebRtcProductPolicy::selectedPairIsNonRelay(*context.connectionResult);
    return result;
}

std::vector<WebRtcProductDiagnostics>
WebRtcProductSessionController::diagnosticsSnapshots() const
{
    std::vector<WebRtcProductDiagnostics> result;
    result.reserve(sessions_.size());
    for (const auto &[streamId, unused] : sessions_) {
        Q_UNUSED(unused);
        result.push_back(diagnosticsSnapshot(streamId));
    }
    return result;
}

QString WebRtcProductSessionController::exchangeRoot() const
{
    return exchangeRootOverride_.isEmpty()
               ? defaultExchangeRoot()
               : QDir::cleanPath(exchangeRootOverride_);
}

QString WebRtcProductSessionController::exchangeRoot(StreamId streamId) const
{
    const auto found = sessions_.find(streamId);
    if (found == sessions_.end()) return {};
    return QDir(exchangeRoot()).filePath(
        QStringLiteral("session-%1").arg(
            found->second->slot, 2, 10, QLatin1Char('0')
        )
    );
}

bool WebRtcProductSessionController::cleanupGlobal(
    std::chrono::milliseconds timeout
) noexcept
{
    try {
        auto cleanup = rtc::Cleanup();
        if (cleanup.wait_for(timeout) == std::future_status::timeout) {
            return false;
        }
        cleanup.get();
        return true;
    } catch (...) {
        return false;
    }
}

void WebRtcProductSessionController::showStartDialog()
{
    if (sessions_.size() >= 4U) return;
    const int slot = lowestFreeSlot();
    WebRtcSessionDialog dialog(
        QDir(exchangeRoot()).filePath(
            QStringLiteral("session-%1").arg(
                slot, 2, 10, QLatin1Char('0')
            )
        ),
        mainWindow_
    );
    while (dialog.exec() == QDialog::Accepted) {
        QString error;
        if (start(dialog.request(), &error)) return;
        QMessageBox::warning(mainWindow_, tr("无法启动 WebRTC 接收"), error);
    }
}

void WebRtcProductSessionController::handleSessionEvent(
    StreamId streamId,
    std::uint64_t token,
    rtmp_monitor::webrtc_runtime::ReceiveSessionEvent event
)
{
    const auto found = sessions_.find(streamId);
    if (found == sessions_.end() || token != found->second->token) return;
    SessionContext &context = *found->second;
    using EventKind = rtmp_monitor::webrtc_runtime::ReceiveSessionEventKind;
    switch (event.kind) {
    case EventKind::Started:
        setState(streamId, WebRtcProductState::Connecting,
                 tr("等待受管目录中的 Offer/Answer 会话包..."));
        break;
    case EventKind::DescriptionExported:
        setState(streamId, WebRtcProductState::Connecting,
                 tr("本机会话包已生成，请完成文件交换..."));
        publishEvent(streamId, WebRtcProductEventKind::DescriptionExported);
        logEvent(streamId, QStringLiteral("description_exported"),
                 QStringLiteral("A managed WebRTC description was exported."));
        break;
    case EventKind::Connected:
        context.connectionResult = event.connection;
        context.connectedTimer.restart();
        setState(streamId, WebRtcProductState::Connecting,
                 tr("WebRTC 已连接，等待当前画面真实呈现..."));
        logEvent(streamId, QStringLiteral("transport_connected"),
                 QStringLiteral("WebRTC transport connected; awaiting presentation."));
        break;
    case EventKind::ConnectionLost:
        setState(streamId, WebRtcProductState::Error,
                 tr("WebRTC 连接已中断；不会自动回退 RTMP"));
        publishEvent(streamId, WebRtcProductEventKind::Failed,
                     QStringLiteral("connection_lost"));
        logEvent(streamId, QStringLiteral("connection_lost"),
                 QStringLiteral("WebRTC connection was lost."),
                 QStringLiteral("connection_lost"));
        break;
    case EventKind::Failed: {
        const WebRtcProductState failureState =
            WebRtcProductPolicy::classifyConnectionFailure(
                event.connection.error,
                event.connection.iceState,
                event.connection.candidateTypes
            );
        setState(streamId, failureState, statusForFailure(failureState));
        publishEvent(
            streamId,
            failureState == WebRtcProductState::NeedsRelay
                ? WebRtcProductEventKind::NeedsRelay
                : WebRtcProductEventKind::Failed,
            event.reason
        );
        logEvent(
            streamId,
            failureState == WebRtcProductState::NeedsRelay
                ? QStringLiteral("needs_relay")
                : QStringLiteral("session_failed"),
            failureState == WebRtcProductState::NeedsRelay
                ? QStringLiteral("ICE checks exhausted with srflx evidence.")
                : QStringLiteral("One-shot WebRTC receive session failed."),
            event.reason
        );
        break;
    }
    case EventKind::Cancelled:
        break;
    }
}

void WebRtcProductSessionController::pollDiagnostics()
{
    const std::vector<StreamId> ids = activeStreamIds();
    for (const StreamId streamId : ids) {
        auto found = sessions_.find(streamId);
        if (found == sessions_.end()) continue;
        const std::uint64_t token = found->second->token;
        const WebRtcProductDiagnostics diagnostics =
            diagnosticsSnapshot(streamId);
        emit diagnosticsChanged(diagnostics);
        found = sessions_.find(streamId);
        if (found == sessions_.end() || found->second->token != token ||
            !found->second->connectionResult.has_value()) continue;

        const bool fresh = WebRtcProductPolicy::hasFreshDirectEvidence(
            *found->second->connectionResult,
            diagnostics,
            kPresentationFreshnessMs
        );
        if (fresh) {
            const bool recovering = found->second->directObserved &&
                found->second->state == WebRtcProductState::Error;
            if (found->second->state != WebRtcProductState::Direct) {
                found->second->directObserved = true;
                if (found->second->videoWidget) {
                    found->second->videoWidget->showFrame();
                }
                setState(streamId, WebRtcProductState::Direct,
                         tr("Direct · 当前画面已呈现"));
                found = sessions_.find(streamId);
                if (found == sessions_.end() ||
                    found->second->token != token) continue;
                publishEvent(
                    streamId,
                    recovering ? WebRtcProductEventKind::MediaRecovered
                               : WebRtcProductEventKind::DirectEstablished
                );
                found = sessions_.find(streamId);
                if (found == sessions_.end() ||
                    found->second->token != token) continue;
                logEvent(
                    streamId,
                    recovering ? QStringLiteral("media_recovered")
                               : QStringLiteral("direct_established"),
                    recovering
                        ? QStringLiteral(
                              "Current-generation presentation recovered."
                          )
                        : QStringLiteral(
                              "Non-relay pair and fresh presentation observed."
                          )
                );
            }
            continue;
        }

        if (found->second->state == WebRtcProductState::Direct &&
            diagnostics.presentedFrameAgeMs > kPresentationFreshnessMs) {
            setState(streamId, WebRtcProductState::Error,
                     tr("画面已超过 1,000 ms 未呈现；控制仍未授权"));
            found = sessions_.find(streamId);
            if (found == sessions_.end() ||
                found->second->token != token) continue;
            publishEvent(streamId, WebRtcProductEventKind::MediaInterrupted,
                         QStringLiteral("presentation_stale"));
            found = sessions_.find(streamId);
            if (found == sessions_.end() ||
                found->second->token != token) continue;
            logEvent(streamId, QStringLiteral("media_interrupted"),
                     QStringLiteral("Current presentation became stale."),
                     QStringLiteral("presentation_stale"));
        } else if (found->second->state == WebRtcProductState::Connecting &&
                   found->second->connectedTimer.isValid() &&
                   found->second->connectedTimer.elapsed() >
                       kInitialMediaTimeoutMs) {
            setState(streamId, WebRtcProductState::Error,
                     tr("已连接但 10 秒内没有真实呈现画面"));
            found = sessions_.find(streamId);
            if (found == sessions_.end() ||
                found->second->token != token) continue;
            publishEvent(streamId, WebRtcProductEventKind::Failed,
                         QStringLiteral("media_timeout"));
            found = sessions_.find(streamId);
            if (found == sessions_.end() ||
                found->second->token != token) continue;
            logEvent(
                streamId, QStringLiteral("media_timeout"),
                QStringLiteral("Transport connected without presentation."),
                QStringLiteral("media_timeout")
            );
        }
    }
}

void WebRtcProductSessionController::setState(
    StreamId streamId,
    WebRtcProductState state,
    const QString &statusText
)
{
    const auto found = sessions_.find(streamId);
    if (found == sessions_.end()) return;
    SessionContext &context = *found->second;
    if (context.videoWidget && !statusText.isEmpty()) {
        context.videoWidget->setStatusText(statusText);
    }
    if (context.state == state) return;
    context.state = state;
    emit streamStateChanged(streamId, state);
    const WebRtcProductState aggregate = this->state();
    if (lastAggregateState_ != aggregate) {
        lastAggregateState_ = aggregate;
        emit stateChanged(aggregate);
    }
}

void WebRtcProductSessionController::publishEvent(
    StreamId streamId,
    WebRtcProductEventKind kind,
    const QString &reason
)
{
    emit eventObserved({kind, reason, streamId});
}

void WebRtcProductSessionController::logEvent(
    StreamId streamId,
    const QString &eventName,
    const QString &message,
    const QString &reason
)
{
    if (!logManager_) return;
    QJsonObject fields {
        {QStringLiteral("state"),
         QString::fromLatin1(WebRtcProductPolicy::stateName(state(streamId)))},
        {QStringLiteral("streamId"), static_cast<double>(streamId)},
        {QStringLiteral("controlAuthorized"), false},
        {QStringLiteral("rtmpFallbackStarted"), false}
    };
    if (!reason.isEmpty()) fields.insert(QStringLiteral("reason"), reason);
    const auto found = sessions_.find(streamId);
    if (found != sessions_.end() && found->second->connectionResult.has_value() &&
        found->second->connectionResult->selectedPair.has_value()) {
        const auto &pair = *found->second->connectionResult->selectedPair;
        fields.insert(QStringLiteral("localType"),
                      QString::fromStdString(pair.localType));
        fields.insert(QStringLiteral("remoteType"),
                      QString::fromStdString(pair.remoteType));
        fields.insert(QStringLiteral("transport"),
                      QString::fromStdString(pair.localTransport));
    }
    logManager_->logSystem(
        eventName.contains(QStringLiteral("failed")) ||
                eventName.contains(QStringLiteral("timeout"))
            ? LogLevel::Warning : LogLevel::Info,
        QStringLiteral("webrtc_product"), eventName, message, fields
    );
}

void WebRtcProductSessionController::releaseSessionObjects(
    StreamId streamId,
    bool removeWidget
)
{
    std::unique_ptr<SessionContext> context = detachSession(streamId);
    if (!context) return;
    context->token = ++nextSessionToken_;
    releaseDetachedSession(streamId, std::move(context), removeWidget);
}

std::unique_ptr<WebRtcProductSessionController::SessionContext>
WebRtcProductSessionController::detachSession(StreamId streamId)
{
    const auto found = sessions_.find(streamId);
    if (found == sessions_.end()) return {};
    std::unique_ptr<SessionContext> context = std::move(found->second);
    sessions_.erase(found);
    return context;
}

void WebRtcProductSessionController::releaseDetachedSession(
    StreamId streamId,
    std::unique_ptr<SessionContext> context,
    bool removeWidget
)
{
    if (!context) return;
    if (context->session) {
        context->session->requestStop();
        context->session->join();
        context->session.reset();
    }
    if (context->inputHandle) context->inputHandle->close();
    context->inputHandle.reset();
    context->mailbox.reset();
    (void)playbackManager_->removeStream(streamId);
    if (removeWidget && context->videoWidget) {
        removeWidgetOrRetry(context->videoWidget);
    }
    const WebRtcProductState aggregate = state();
    if (lastAggregateState_ != aggregate) {
        lastAggregateState_ = aggregate;
        emit stateChanged(aggregate);
    }
    updateActionsAndTimer();
}

void WebRtcProductSessionController::removeWidgetOrRetry(
    QPointer<VideoWidget> widget
)
{
    if (!widget || mainWindow_->removeConnectionWidget(widget)) return;
    const auto pending = std::find(
        pendingWidgetRemovals_.begin(), pendingWidgetRemovals_.end(), widget
    );
    if (pending == pendingWidgetRemovals_.end()) {
        if (pendingWidgetRemovals_.size() < 4U) {
            pendingWidgetRemovals_.push_back(widget);
        } else {
            QTimer::singleShot(50, this, [this, widget] {
                removeWidgetOrRetry(widget);
            });
            return;
        }
    }
    if (!widgetRemovalRetryScheduled_) {
        widgetRemovalRetryScheduled_ = true;
        QTimer::singleShot(50, this, [this] {
            retryPendingWidgetRemovals();
        });
    }
}

void WebRtcProductSessionController::retryPendingWidgetRemovals()
{
    widgetRemovalRetryScheduled_ = false;
    std::vector<QPointer<VideoWidget>> remaining;
    remaining.reserve(pendingWidgetRemovals_.size());
    for (const QPointer<VideoWidget> &widget : pendingWidgetRemovals_) {
        if (widget && !mainWindow_->removeConnectionWidget(widget)) {
            remaining.push_back(widget);
        }
    }
    pendingWidgetRemovals_ = std::move(remaining);
    if (!pendingWidgetRemovals_.empty()) {
        widgetRemovalRetryScheduled_ = true;
        QTimer::singleShot(50, this, [this] {
            retryPendingWidgetRemovals();
        });
    }
}

void WebRtcProductSessionController::updateActionsAndTimer()
{
    if (startAction_) startAction_->setEnabled(sessions_.size() < 4U);
    if (cancelAction_) cancelAction_->setEnabled(!sessions_.empty());
    if (diagnosticsTimer_) {
        if (sessions_.empty()) diagnosticsTimer_->stop();
        else if (!diagnosticsTimer_->isActive()) diagnosticsTimer_->start();
    }
}

int WebRtcProductSessionController::lowestFreeSlot() const noexcept
{
    std::array<bool, 4> used {false, false, false, false};
    for (const auto &[unused, context] : sessions_) {
        Q_UNUSED(unused);
        if (context->slot >= 1 && context->slot <= 4) {
            used[static_cast<std::size_t>(context->slot - 1)] = true;
        }
    }
    for (std::size_t index = 0; index < used.size(); ++index) {
        if (!used[index]) return static_cast<int>(index + 1);
    }
    return 0;
}

QString WebRtcProductSessionController::defaultExchangeRoot() const
{
    return QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
    ).filePath(QStringLiteral("webrtc-session-exchange"));
}

} // namespace rtmp_monitor::webrtc_product
