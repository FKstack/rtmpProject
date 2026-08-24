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

#include <future>
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
    cancelAction_ = menu_->addAction(tr("取消当前会话"));
    cancelAction_->setObjectName(QStringLiteral("cancelOneShotWebRtcAction"));
    cancelAction_->setEnabled(false);
    connect(startAction_, &QAction::triggered,
            this, &WebRtcProductSessionController::showStartDialog);
    connect(cancelAction_, &QAction::triggered,
            this, &WebRtcProductSessionController::cancel);

    diagnosticsTimer_ = new QTimer(this);
    diagnosticsTimer_->setInterval(100);
    connect(diagnosticsTimer_, &QTimer::timeout,
            this, &WebRtcProductSessionController::pollDiagnostics);
}

WebRtcProductSessionController::~WebRtcProductSessionController()
{
    cancel();
}

bool WebRtcProductSessionController::start(
    WebRtcSessionRequest request,
    QString *error
)
{
    if (isActive()) {
        if (error) *error = tr("请先取消当前一次性 WebRTC 会话。");
        return false;
    }
    if (!WebRtcProductPolicy::validateRequest(request, error)) return false;

    EncodedVideoInputHandle input =
        playbackManager_->createEncodedVideoInput(request.displayName.trimmed());
    if (!input.isOpen()) {
        if (error) *error = tr("无法创建接收解码入口；请检查 16 路容量。");
        return false;
    }
    inputHandle_ = std::make_shared<EncodedVideoInputHandle>(std::move(input));
    mailbox_ = playbackManager_->frameMailbox(inputHandle_->streamId());
    videoWidget_ = mainWindow_->addConnectionWidget(request.displayName.trimmed());
    if (!videoWidget_ || !mailbox_) {
        releaseSessionObjects(true);
        if (error) *error = tr("无法创建 WebRTC 视频显示格。");
        return false;
    }
    mainWindow_->bindVideoStream(
        videoWidget_, inputHandle_->streamId(), mailbox_
    );
    // Keep the render item active while the status overlay is visible.  Direct
    // is still withheld until the mailbox reports an actually presented frame.
    videoWidget_->showFrame();
    videoWidget_->setAudioPlaybackState(AudioPlaybackState::Unavailable, false);
    videoWidget_->setControlTargetSelected(false);
    connect(videoWidget_, &VideoWidget::removeRequested,
            this, [this](VideoWidget *) { cancel(); });

    const std::weak_ptr<EncodedVideoInputHandle> weakInput(inputHandle_);
    rtmp_monitor::webrtc_runtime::WebRtcReceiveSessionOptions options;
    options.exchangeRoot = exchangeRoot();
    options.signalingRole = request.signalingRole;
    options.ice = std::move(request.ice);

    const std::uint64_t token = ++sessionToken_;
    QPointer<WebRtcProductSessionController> target(this);
    session_ = std::make_unique<
        rtmp_monitor::webrtc_runtime::WebRtcReceiveSession>(
        std::move(options),
        [weakInput](SessionMediaSample sample) {
            const auto inputHandle = weakInput.lock();
            return inputHandle
                       ? inputHandle->submit(std::move(sample.accessUnit))
                       : H264SubmitResult::Closed;
        },
        [target, token](
            rtmp_monitor::webrtc_runtime::ReceiveSessionEvent event
        ) mutable {
            if (!target) return;
            QMetaObject::invokeMethod(
                target,
                [target, token, event = std::move(event)]() mutable {
                    if (target) {
                        target->handleSessionEvent(token, std::move(event));
                    }
                },
                Qt::QueuedConnection
            );
        }
    );
    directObserved_ = false;
    connectionResult_.reset();
    setState(WebRtcProductState::Connecting,
             tr("正在准备受管 Offer/Answer 交换..."));
    startAction_->setEnabled(false);
    cancelAction_->setEnabled(true);
    diagnosticsTimer_->start();
    publishEvent(WebRtcProductEventKind::SessionStarted);
    logEvent(QStringLiteral("session_started"),
             QStringLiteral("One-shot WebRTC receive session started."));
    if (!session_->start()) {
        releaseSessionObjects(true);
        setState(WebRtcProductState::Idle, {});
        if (error) *error = tr("一次性 WebRTC worker 无法启动。");
        return false;
    }
    if (error) error->clear();
    return true;
}

void WebRtcProductSessionController::cancel()
{
    if (!isActive()) return;
    ++sessionToken_;
    if (diagnosticsTimer_) diagnosticsTimer_->stop();
    if (session_) {
        session_->requestStop();
        session_->join();
    }
    publishEvent(WebRtcProductEventKind::Cancelled);
    logEvent(QStringLiteral("session_cancelled"),
             QStringLiteral("One-shot WebRTC receive session cancelled."));
    releaseSessionObjects(true);
    setState(WebRtcProductState::Idle, {});
}

bool WebRtcProductSessionController::isActive() const noexcept
{
    return inputHandle_ != nullptr;
}

WebRtcProductState WebRtcProductSessionController::state() const noexcept
{
    return state_;
}

WebRtcProductDiagnostics
WebRtcProductSessionController::diagnosticsSnapshot() const
{
    WebRtcProductDiagnostics result;
    result.state = state_;
    if (session_) result.transport = session_->snapshot();
    if (inputHandle_) {
        result.media = playbackManager_->streamMetrics(inputHandle_->streamId());
    }
    result.presentedFrameAgeMs =
        mailbox_ ? mailbox_->lastPresentedFrameAgeMs() : -1;
    result.selectedNonRelayPair = connectionResult_.has_value() &&
        WebRtcProductPolicy::selectedPairIsNonRelay(*connectionResult_);
    return result;
}

QString WebRtcProductSessionController::exchangeRoot() const
{
    return exchangeRootOverride_.isEmpty()
               ? defaultExchangeRoot()
               : QDir::cleanPath(exchangeRootOverride_);
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
    if (isActive()) return;
    WebRtcSessionDialog dialog(exchangeRoot(), mainWindow_);
    while (dialog.exec() == QDialog::Accepted) {
        QString error;
        if (start(dialog.request(), &error)) return;
        QMessageBox::warning(mainWindow_, tr("无法启动 WebRTC 接收"), error);
    }
}

void WebRtcProductSessionController::handleSessionEvent(
    std::uint64_t token,
    rtmp_monitor::webrtc_runtime::ReceiveSessionEvent event
)
{
    if (token != sessionToken_ || !isActive()) return;
    using EventKind = rtmp_monitor::webrtc_runtime::ReceiveSessionEventKind;
    switch (event.kind) {
    case EventKind::Started:
        setState(WebRtcProductState::Connecting,
                 tr("等待受管目录中的 Offer/Answer 会话包..."));
        break;
    case EventKind::DescriptionExported:
        setState(WebRtcProductState::Connecting,
                 tr("本机会话包已生成，请完成文件交换..."));
        publishEvent(WebRtcProductEventKind::DescriptionExported);
        logEvent(QStringLiteral("description_exported"),
                 QStringLiteral("A managed WebRTC description was exported."));
        break;
    case EventKind::Connected:
        connectionResult_ = event.connection;
        connectedTimer_.restart();
        setState(WebRtcProductState::Connecting,
                 tr("WebRTC 已连接，等待当前画面真实呈现..."));
        logEvent(QStringLiteral("transport_connected"),
                 QStringLiteral("WebRTC transport connected; awaiting presentation."));
        break;
    case EventKind::ConnectionLost:
        setState(WebRtcProductState::Error,
                 tr("WebRTC 连接已中断；不会自动回退 RTMP"));
        publishEvent(WebRtcProductEventKind::Failed,
                     QStringLiteral("connection_lost"));
        logEvent(QStringLiteral("connection_lost"),
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
        setState(failureState, statusForFailure(failureState));
        publishEvent(
            failureState == WebRtcProductState::NeedsRelay
                ? WebRtcProductEventKind::NeedsRelay
                : WebRtcProductEventKind::Failed,
            event.reason
        );
        logEvent(
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
    if (!isActive()) return;
    const WebRtcProductDiagnostics diagnostics = diagnosticsSnapshot();
    emit diagnosticsChanged(diagnostics);
    if (!connectionResult_.has_value()) return;

    const bool fresh = WebRtcProductPolicy::hasFreshDirectEvidence(
        *connectionResult_, diagnostics, kPresentationFreshnessMs
    );
    if (fresh) {
        const bool recovering = directObserved_ &&
            state_ == WebRtcProductState::Error;
        if (state_ != WebRtcProductState::Direct) {
            if (videoWidget_) videoWidget_->showFrame();
            setState(WebRtcProductState::Direct, tr("Direct · 当前画面已呈现"));
            publishEvent(
                recovering ? WebRtcProductEventKind::MediaRecovered
                           : WebRtcProductEventKind::DirectEstablished
            );
            logEvent(
                recovering ? QStringLiteral("media_recovered")
                           : QStringLiteral("direct_established"),
                recovering
                    ? QStringLiteral("Current-generation presentation recovered.")
                    : QStringLiteral("Non-relay pair and fresh presentation observed.")
            );
            directObserved_ = true;
        }
        return;
    }

    if (state_ == WebRtcProductState::Direct &&
        diagnostics.presentedFrameAgeMs > kPresentationFreshnessMs) {
        setState(WebRtcProductState::Error,
                 tr("画面已超过 1,000 ms 未呈现；控制仍未授权"));
        publishEvent(WebRtcProductEventKind::MediaInterrupted,
                     QStringLiteral("presentation_stale"));
        logEvent(QStringLiteral("media_interrupted"),
                 QStringLiteral("Current presentation became stale."),
                 QStringLiteral("presentation_stale"));
    } else if (state_ == WebRtcProductState::Connecting &&
               connectedTimer_.isValid() &&
               connectedTimer_.elapsed() > kInitialMediaTimeoutMs) {
        setState(WebRtcProductState::Error,
                 tr("已连接但 10 秒内没有真实呈现画面"));
        publishEvent(WebRtcProductEventKind::Failed,
                     QStringLiteral("media_timeout"));
        logEvent(QStringLiteral("media_timeout"),
                 QStringLiteral("Transport connected without presentation."),
                 QStringLiteral("media_timeout"));
    }
}

void WebRtcProductSessionController::setState(
    WebRtcProductState state,
    const QString &statusText
)
{
    if (videoWidget_ && !statusText.isEmpty()) {
        videoWidget_->setStatusText(statusText);
    }
    if (state_ == state) return;
    state_ = state;
    emit stateChanged(state_);
}

void WebRtcProductSessionController::publishEvent(
    WebRtcProductEventKind kind,
    const QString &reason
)
{
    emit eventObserved({kind, reason});
}

void WebRtcProductSessionController::logEvent(
    const QString &eventName,
    const QString &message,
    const QString &reason
)
{
    if (!logManager_) return;
    QJsonObject fields {
        {QStringLiteral("state"),
         QString::fromLatin1(WebRtcProductPolicy::stateName(state_))},
        {QStringLiteral("controlAuthorized"), false},
        {QStringLiteral("rtmpFallbackStarted"), false}
    };
    if (!reason.isEmpty()) fields.insert(QStringLiteral("reason"), reason);
    if (connectionResult_.has_value() &&
        connectionResult_->selectedPair.has_value()) {
        const auto &pair = *connectionResult_->selectedPair;
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

void WebRtcProductSessionController::releaseSessionObjects(bool removeWidget)
{
    if (diagnosticsTimer_) diagnosticsTimer_->stop();
    if (session_) {
        session_->requestStop();
        session_->join();
        session_.reset();
    }
    const StreamId streamId = inputHandle_ ? inputHandle_->streamId()
                                           : kInvalidStreamId;
    if (inputHandle_) inputHandle_->close();
    inputHandle_.reset();
    mailbox_.reset();
    if (streamId != kInvalidStreamId) {
        (void)playbackManager_->removeStream(streamId);
    }
    if (removeWidget && videoWidget_) {
        (void)mainWindow_->removeConnectionWidget(videoWidget_);
    }
    videoWidget_.clear();
    connectionResult_.reset();
    directObserved_ = false;
    connectedTimer_.invalidate();
    if (startAction_) startAction_->setEnabled(true);
    if (cancelAction_) cancelAction_->setEnabled(false);
}

QString WebRtcProductSessionController::defaultExchangeRoot() const
{
    return QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
    ).filePath(QStringLiteral("webrtc-session-exchange"));
}

} // namespace rtmp_monitor::webrtc_product
