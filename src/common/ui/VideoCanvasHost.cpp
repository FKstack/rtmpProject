#include "ui/VideoCanvasHost.h"

#include <QResizeEvent>
#include <QShowEvent>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <utility>

#include "RtmpMonitorBuildConfig.h"
#include "ui/CpuVideoCanvas.h"
#if RTMP_MONITOR_HAS_OPENGL
#include "ui/VideoOpenGLCanvas.h"
#endif

namespace {

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           )
        .count();
}

QString rendererPreferenceName(RendererPreference preference)
{
    switch (preference) {
    case RendererPreference::Auto:
        return QStringLiteral("auto");
    case RendererPreference::OpenGL:
        return QStringLiteral("opengl");
    case RendererPreference::Cpu:
        return QStringLiteral("cpu");
    }
    return QStringLiteral("unknown");
}

} // namespace

VideoCanvasHost::VideoCanvasHost(
    RendererPreference preference,
    QWidget *parent
)
    : QWidget(parent)
    , preference_(preference)
    , controller_(std::make_unique<VideoRenderController>())
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    scheduleTimer_ = new QTimer(this);
    scheduleTimer_->setTimerType(Qt::PreciseTimer);
    scheduleTimer_->setInterval(
        std::max(1, qRound(1000.0 / static_cast<double>(targetFps_)))
    );
    connect(scheduleTimer_, &QTimer::timeout, this, &VideoCanvasHost::scheduleTick);
    createBackend();
    scheduleTimer_->start();
}

VideoCanvasHost::~VideoCanvasHost()
{
    scheduleTimer_->stop();
    controller_->clearStreams();
#if RTMP_MONITOR_HAS_OPENGL
    delete openGLCanvas_;
    openGLCanvas_ = nullptr;
#endif
    delete cpuCanvas_;
    cpuCanvas_ = nullptr;
}

VideoRenderController *VideoCanvasHost::controller() noexcept
{
    return controller_.get();
}

const VideoRenderController *VideoCanvasHost::controller() const noexcept
{
    return controller_.get();
}

void VideoCanvasHost::setSnapshot(RenderSnapshot snapshot)
{
    controller_->setSnapshot(std::move(snapshot));
}

void VideoCanvasHost::registerStream(
    StreamId streamId,
    std::shared_ptr<LatestFrameMailbox> mailbox
)
{
    controller_->registerStream(streamId, std::move(mailbox));
}

void VideoCanvasHost::unregisterStream(StreamId streamId)
{
    controller_->unregisterStream(streamId);
}

void VideoCanvasHost::setTargetFps(int fps)
{
    targetFps_ = std::clamp(fps, 1, 60);
    scheduleTimer_->setInterval(
        std::max(1, qRound(1000.0 / static_cast<double>(targetFps_)))
    );
}

void VideoCanvasHost::setRendererPreference(RendererPreference preference)
{
    if (preference_ == preference) {
        return;
    }
    preference_ = preference;
#if RTMP_MONITOR_HAS_OPENGL
    if (openGLCanvas_ != nullptr) {
        openGLCanvas_->hide();
        delete openGLCanvas_;
        openGLCanvas_ = nullptr;
    }
#endif
    if (cpuCanvas_ != nullptr) {
        cpuCanvas_->hide();
        delete cpuCanvas_;
        cpuCanvas_ = nullptr;
    }
    cpuActive_ = false;
    createBackend();
}

RendererPreference VideoCanvasHost::rendererPreference() const noexcept
{
    return preference_;
}

QString VideoCanvasHost::activeBackendName() const
{
    return cpuActive_ ? QStringLiteral("cpu") : QStringLiteral("opengl");
}

RenderStatistics VideoCanvasHost::statistics() const noexcept
{
    RenderStatistics result = statistics_;
    result.dirtyMerges = controller_->dirtyState()->mergeCount();
    return result;
}

RenderRuntimeMetrics VideoCanvasHost::runtimeMetrics() const
{
    const RenderStatistics renderStatistics = statistics();
    RenderRuntimeMetrics metrics;
    metrics.requestedBackend = rendererPreferenceName(preference_);
    metrics.activeBackend = activeBackendName();
    metrics.fallbackOccurred = fallbackOccurred_;
    metrics.fallbackReason = fallbackReason_;
    metrics.graphicsApi = graphicsApi_;
    metrics.openGlVendor = openGlVendor_;
    metrics.openGlRenderer = openGlRenderer_;
    metrics.openGlVersion = openGlVersion_;
    metrics.scheduleChecks = renderStatistics.scheduleChecks;
    metrics.updateRequests = renderStatistics.updateRequests;
    metrics.dirtyMerges = renderStatistics.dirtyMerges;
    metrics.paintCalls = renderStatistics.paintCalls;
    metrics.uploadedFrames = renderStatistics.uploadedFrames;
    metrics.renderedFrames = renderStatistics.renderedFrames;
    metrics.unsupportedFrames = renderStatistics.unsupportedFrames;
    metrics.paintCpuUs = renderStatistics.lastPaintCpuUs;
    metrics.uploadCpuUs = renderStatistics.lastUploadCpuUs;
    metrics.gpuTimeUs = renderStatistics.lastGpuTimeUs;
    metrics.latestFrameAgeMs = renderStatistics.latestFrameAgeMs;
    metrics.textureBytes = static_cast<qint64>(renderStatistics.textureBytes);
    const RenderSnapshot &snapshot = controller_->snapshot();
    metrics.renderItemCount = static_cast<int>(snapshot.items.size());
    for (const RenderItem &item : snapshot.items) {
        if (item.frameVisible) {
            ++metrics.visibleRenderItemCount;
        }
        if (controller_->mailbox(item.streamId) != nullptr) {
            ++metrics.boundMailboxCount;
        }
    }
    return metrics;
}

QImage VideoCanvasHost::grabFramebufferImage()
{
    QImage image;
    bool openGlFramebuffer = false;
#if RTMP_MONITOR_HAS_OPENGL
    if (!cpuActive_ && openGLCanvas_ != nullptr &&
        openGLCanvas_->isValid()) {
        image = openGLCanvas_->grabFramebuffer();
        openGlFramebuffer = true;
    } else
#endif
    if (cpuCanvas_ != nullptr) {
        image = cpuCanvas_->grab().toImage();
    }
    if (!image.isNull() && openGlFramebuffer) {
        image.setDevicePixelRatio(devicePixelRatioF());
    }
    return image;
}

void VideoCanvasHost::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
#if RTMP_MONITOR_HAS_OPENGL
    if (openGLCanvas_ != nullptr) {
        openGLCanvas_->setGeometry(rect());
    }
#endif
    if (cpuCanvas_ != nullptr) {
        cpuCanvas_->setGeometry(rect());
    }
    controller_->markDirty(RenderDirtyFlag::Viewport);
}

void VideoCanvasHost::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    controller_->markDirty(RenderDirtyFlag::Viewport);
    paintPending_ = false;
}

void VideoCanvasHost::createBackend()
{
    fallbackOccurred_ = false;
    fallbackReason_.clear();
    graphicsApi_.clear();
    openGlVendor_.clear();
    openGlRenderer_.clear();
    openGlVersion_.clear();
    if (preference_ == RendererPreference::Cpu) {
        activateCpuFallback({});
        return;
    }
#if RTMP_MONITOR_HAS_OPENGL
    openGLCanvas_ = new VideoOpenGLCanvas(this, this);
    openGLCanvas_->setGeometry(rect());
    openGLCanvas_->show();
    openGLCanvas_->lower();
    cpuActive_ = false;
    controller_->markDirty(RenderDirtyFlag::Resource);
#else
    activateCpuFallback(
        preference_ == RendererPreference::OpenGL
            ? QStringLiteral(
                  "This build does not include an OpenGL backend "
                  "(RTMP_MONITOR_HAS_OPENGL=0); falling back to CPU."
              )
            : QStringLiteral(
                  "OpenGL backend not included in this build; using CPU."
              )
    );
#endif
}

void VideoCanvasHost::activateCpuFallback(const QString &reason)
{
#if RTMP_MONITOR_HAS_OPENGL
    if (openGLCanvas_ != nullptr) {
        openGLCanvas_->hide();
    }
#endif
    if (cpuCanvas_ == nullptr) {
        cpuCanvas_ = new CpuVideoCanvas(this, this);
        cpuCanvas_->setGeometry(rect());
        cpuCanvas_->lower();
    }
    cpuCanvas_->show();
    cpuActive_ = true;
    fallbackOccurred_ = preference_ != RendererPreference::Cpu && !reason.isEmpty();
    fallbackReason_ = reason;
    controller_->markDirty(RenderDirtyFlag::Resource);
    emit backendChanged(QStringLiteral("cpu"));
    if (!reason.isEmpty()) {
        emit renderingError(reason);
    }
}

void VideoCanvasHost::scheduleTick()
{
    ++statistics_.scheduleChecks;
    if (!isVisible() || paintPending_ || controller_->pendingDirty() == 0U) {
        return;
    }
    paintPending_ = true;
    ++statistics_.updateRequests;
    if (cpuActive_ && cpuCanvas_ != nullptr) {
        cpuCanvas_->update();
        return;
    }
#if RTMP_MONITOR_HAS_OPENGL
    if (openGLCanvas_ != nullptr) {
        openGLCanvas_->update();
        return;
    }
#endif
    paintPending_ = false;
}

void VideoCanvasHost::onSurfacePainted()
{
    paintPending_ = false;
    lastPaintMonotonicMs_ = monotonicMilliseconds();
    statistics_.dirtyMerges = controller_->dirtyState()->mergeCount();
    for (const RenderItem &item : controller_->snapshot().items) {
        if (const auto mailbox = controller_->mailbox(item.streamId);
            mailbox != nullptr) {
            mailbox->setRenderDiagnostics(
                statistics_.lastUploadCpuUs,
                statistics_.lastPaintCpuUs,
                statistics_.dirtyMerges,
                statistics_.scheduleChecks,
                static_cast<qint64>(statistics_.textureBytes)
            );
        }
    }
}

void VideoCanvasHost::onOpenGLInitialized(
    bool success,
    const QString &error,
    const QString &graphicsApi,
    const QString &vendor,
    const QString &renderer,
    const QString &version
)
{
    graphicsApi_ = graphicsApi;
    openGlVendor_ = vendor;
    openGlRenderer_ = renderer;
    openGlVersion_ = version;
    if (success) {
        cpuActive_ = false;
        fallbackOccurred_ = false;
        fallbackReason_.clear();
        emit backendChanged(QStringLiteral("opengl"));
        controller_->markDirty(RenderDirtyFlag::Resource);
        return;
    }
    activateCpuFallback(error);
}
