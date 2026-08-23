#include "webrtc_client/WebRtcViewerController.h"

#include "ui/VideoCanvasHost.h"

#include <QCloseEvent>
#include <QEvent>
#include <QImage>
#include <QJsonObject>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <stdexcept>
#include <utility>

namespace rtmp_monitor::webrtc_client {

WebRtcViewerController::WebRtcViewerController(
    ClientEventSink eventSink,
    std::shared_ptr<WebRtcViewerEvidence> evidence,
    QObject *parent
)
    : QObject(parent),
      eventSink_(std::move(eventSink)),
      evidence_(std::move(evidence)),
      manager_(PlaybackPerformanceOptions {})
{
    EncodedVideoInputHandle input =
        manager_.createEncodedVideoInput(QStringLiteral("WebRTC viewer"));
    if (!input.isOpen()) {
        throw std::runtime_error("viewer_input_creation_failed");
    }
    streamId_ = input.streamId();
    mailbox_ = manager_.frameMailbox(streamId_);
    if (!mailbox_) {
        input.close();
        (void)manager_.removeStream(streamId_);
        throw std::runtime_error("viewer_mailbox_creation_failed");
    }
    input_ = std::make_shared<EncodedVideoInputHandle>(std::move(input));

    window_ = new QWidget();
    window_->setWindowTitle(QStringLiteral("RtmpMonitor WebRTC Viewer"));
    window_->resize(1280, 720);
    auto *layout = new QVBoxLayout(window_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    canvas_ = new VideoCanvasHost(RendererPreference::Cpu, window_);
    canvas_->setTargetFps(30);
    layout->addWidget(canvas_);
    window_->installEventFilter(this);
    canvas_->installEventFilter(this);
    canvas_->registerStream(streamId_, mailbox_);

    connect(
        &manager_,
        &MultiStreamPlaybackManager::stateChanged,
        this,
        [this](StreamId streamId, DeviceStatus state) {
            if (streamId == streamId_ && state == DeviceStatus::Playing) {
                observeDecodedFrame();
            }
        }
    );
    connect(
        canvas_,
        &VideoCanvasHost::surfacePresented,
        this,
        [this] {
            if (presentationCheckPending_ || presentedEmitted_) return;
            presentationCheckPending_ = true;
            QTimer::singleShot(0, this, [this] { observePresentedFrame(); });
        }
    );
    updateSnapshot();
}

WebRtcViewerController::~WebRtcViewerController()
{
    shutdown();
}

rtmp_monitor::webrtc_transport::H264ReceiveSink
WebRtcViewerController::receiveSink() const
{
    const std::weak_ptr<EncodedVideoInputHandle> weakInput(input_);
    return [weakInput](SessionMediaSample sample) {
        const auto input = weakInput.lock();
        if (!input) return H264SubmitResult::Closed;
        return input->submit(std::move(sample.accessUnit));
    };
}

void WebRtcViewerController::setCloseCallback(
    std::function<void()> callback
)
{
    closeCallback_ = std::move(callback);
}

void WebRtcViewerController::show()
{
    if (window_) window_->show();
}

void WebRtcViewerController::shutdown()
{
    if (shutdown_) return;
    shutdown_ = true;
    if (canvas_ && streamId_ != kInvalidStreamId) {
        canvas_->unregisterStream(streamId_);
    }
    if (input_) input_->close();
    if (streamId_ != kInvalidStreamId) {
        (void)manager_.removeStream(streamId_);
    }
    input_.reset();
    mailbox_.reset();
    streamId_ = kInvalidStreamId;
    if (window_) {
        window_->removeEventFilter(this);
        window_->hide();
        delete window_;
    }
    window_ = nullptr;
    canvas_ = nullptr;
}

bool WebRtcViewerController::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == window_ || watched == canvas_) &&
        event->type() == QEvent::Resize) {
        QTimer::singleShot(0, this, [this] { updateSnapshot(); });
    }
    if (watched == window_ && event->type() == QEvent::Close &&
        !closeNotified_) {
        closeNotified_ = true;
        if (closeCallback_) closeCallback_();
    }
    return QObject::eventFilter(watched, event);
}

void WebRtcViewerController::updateSnapshot()
{
    if (!canvas_ || streamId_ == kInvalidStreamId) return;
    RenderSnapshot snapshot;
    snapshot.generation = ++snapshotGeneration_;
    snapshot.logicalCanvasSize = canvas_->size();
    snapshot.devicePixelRatio = canvas_->devicePixelRatioF();
    RenderItem item;
    item.streamId = streamId_;
    item.tileRect = QRectF(canvas_->rect());
    item.videoViewport = item.tileRect;
    item.displayMode = VideoDisplayMode::Contain;
    item.title = QStringLiteral("WebRTC viewer");
    item.status = QStringLiteral("Playing");
    item.frameVisible = true;
    snapshot.items.push_back(std::move(item));
    canvas_->setSnapshot(std::move(snapshot));
}

void WebRtcViewerController::observeDecodedFrame()
{
    if (decodedEmitted_ || !mailbox_) return;
    const auto frame = mailbox_->latestAfter(0);
    if (!frame.has_value()) {
        QTimer::singleShot(10, this, [this] { observeDecodedFrame(); });
        return;
    }
    decodedEmitted_ = true;
    if (evidence_) {
        evidence_->decoded.store(true, std::memory_order_release);
    }
    QJsonObject details;
    details.insert(
        QStringLiteral("sequence"),
        static_cast<double>(frame->sequence())
    );
    details.insert(QStringLiteral("width"), frame->width());
    details.insert(QStringLiteral("height"), frame->height());
    emitEvent(QStringLiteral("frame_decoded"), std::move(details));
}

void WebRtcViewerController::observePresentedFrame()
{
    presentationCheckPending_ = false;
    if (presentedEmitted_ || !canvas_ || !mailbox_) return;
    const RenderStatistics render = canvas_->statistics();
    const LatestFrameMailboxStats mailbox = mailbox_->stats();
    if (render.renderedFrames == 0 || mailbox.rendered == 0 ||
        !framebufferContainsImage()) {
        return;
    }
    presentedEmitted_ = true;
    if (evidence_) {
        evidence_->presented.store(true, std::memory_order_release);
    }
    QJsonObject details;
    details.insert(
        QStringLiteral("renderedFrames"),
        static_cast<double>(render.renderedFrames)
    );
    details.insert(
        QStringLiteral("mailboxRendered"),
        static_cast<double>(mailbox.rendered)
    );
    details.insert(QStringLiteral("backend"), canvas_->activeBackendName());
    emitEvent(QStringLiteral("frame_presented"), std::move(details));
}

bool WebRtcViewerController::framebufferContainsImage() const
{
    if (!canvas_) return false;
    const QImage image = canvas_->grabFramebufferImage().convertToFormat(
        QImage::Format_RGB32
    );
    if (image.isNull()) return false;
    const int stepX = std::max(1, image.width() / 64);
    const int stepY = std::max(1, image.height() / 36);
    for (int y = 0; y < image.height(); y += stepY) {
        for (int x = 0; x < image.width(); x += stepX) {
            const QRgb pixel = image.pixel(x, y);
            if (qRed(pixel) > 8 || qGreen(pixel) > 8 || qBlue(pixel) > 8) {
                return true;
            }
        }
    }
    return false;
}

void WebRtcViewerController::emitEvent(
    const QString &event,
    QJsonObject details
) const
{
    if (eventSink_) eventSink_(event, std::move(details));
}

} // namespace rtmp_monitor::webrtc_client
