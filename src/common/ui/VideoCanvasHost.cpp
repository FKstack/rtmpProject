#include "ui/VideoCanvasHost.h"

#include <QColor>
#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "media/VideoFrameConverter.h"
#include "render/OpenGLGridRenderer.h"

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

QString openGlString(QOpenGLExtraFunctions *functions, GLenum name)
{
    if (functions == nullptr) {
        return {};
    }
    const auto *value = functions->glGetString(name);
    return value == nullptr
               ? QString {}
               : QString::fromLatin1(reinterpret_cast<const char *>(value));
}

class CpuFrameCache final
{
public:
    std::uint64_t sequence = 0;
    QImage image;
    VideoFrameToImageConverter converter;
};

} // namespace

class VideoOpenGLCanvas final : public QOpenGLWidget
{
public:
    VideoOpenGLCanvas(VideoCanvasHost *host, QWidget *parent)
        : QOpenGLWidget(parent)
        , host_(host)
    {
        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    ~VideoOpenGLCanvas() override
    {
        QObject::disconnect(contextDestructionConnection_);
        cleanup();
    }

protected:
    void initializeGL() override
    {
        QOpenGLContext *openGLContext = context();
        if (openGLContext == nullptr || !openGLContext->isValid()) {
            host_->onOpenGLInitialized(
                false, QStringLiteral("OpenGL context creation failed.")
            );
            return;
        }
        const QSurfaceFormat format = openGLContext->format();
        const bool openGles = openGLContext->isOpenGLES();
        const bool versionReady = openGles
                                      ? format.majorVersion() >= 3
                                      : (format.majorVersion() > 3 ||
                                         (format.majorVersion() == 3 &&
                                          format.minorVersion() >= 3));
        if (!versionReady) {
            host_->onOpenGLInitialized(
                false,
                openGles
                    ? QStringLiteral("OpenGL ES 3.0 or newer is required.")
                    : QStringLiteral("Desktop OpenGL 3.3 or newer is required.")
            );
            return;
        }

        QObject::disconnect(contextDestructionConnection_);
        contextDestructionConnection_ = QObject::connect(
            openGLContext,
            &QOpenGLContext::aboutToBeDestroyed,
            this,
            [this] { cleanup(); },
            Qt::DirectConnection
        );

        QString error;
        QOpenGLExtraFunctions *functions = openGLContext->extraFunctions();
        functions->initializeOpenGLFunctions();
        const bool initialized = renderer_.initialize(functions, openGles, &error);
        host_->onOpenGLInitialized(
            initialized,
            error,
            openGles ? QStringLiteral("OpenGL ES")
                     : QStringLiteral("Desktop OpenGL"),
            openGlString(functions, GL_VENDOR),
            openGlString(functions, GL_RENDERER),
            openGlString(functions, GL_VERSION)
        );
    }

    void resizeGL(int, int) override
    {
        host_->controller_->markDirty(RenderDirtyFlag::Viewport);
    }

    void paintGL() override
    {
        (void)host_->controller_->consumeDirty();
        QString error;
        QOpenGLContext *openGLContext = context();
        const qreal dpr = devicePixelRatioF();
        const QSize framebufferSize(
            std::max(1, qRound(width() * dpr)),
            std::max(1, qRound(height() * dpr))
        );
        const bool rendered = openGLContext != nullptr && renderer_.render(
            openGLContext->extraFunctions(),
            framebufferSize,
            host_->controller_.get(),
            &host_->statistics_,
            &error
        );
        if (!rendered && !error.isEmpty()) {
            emit host_->renderingError(error);
        }
        host_->onSurfacePainted();
    }

private:
    void cleanup()
    {
        QOpenGLContext *openGLContext = context();
        if (!renderer_.isInitialized()) {
            return;
        }
        if (openGLContext == nullptr || !openGLContext->isValid()) {
            renderer_.release(nullptr);
            host_->controller_->markDirty(RenderDirtyFlag::Resource);
            return;
        }
        makeCurrent();
        renderer_.release(openGLContext->extraFunctions());
        doneCurrent();
        host_->controller_->markDirty(RenderDirtyFlag::Resource);
    }

    VideoCanvasHost *host_ = nullptr;
    OpenGLGridRenderer renderer_;
    QMetaObject::Connection contextDestructionConnection_;
};

class CpuVideoCanvas final : public QWidget
{
public:
    CpuVideoCanvas(VideoCanvasHost *host, QWidget *parent)
        : QWidget(parent)
        , host_(host)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QElapsedTimer timer;
        timer.start();
        (void)host_->controller_->consumeDirty();

        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        qint64 latestAge = -1;
        std::unordered_set<StreamId> activeStreams;
        for (const RenderItem &item : host_->controller_->snapshot().items) {
            if (item.streamId != kInvalidStreamId) {
                activeStreams.insert(item.streamId);
            }
            if (!item.frameVisible || item.streamId == kInvalidStreamId) {
                continue;
            }
            auto &cache = caches_[item.streamId];
            if (const auto frame = host_->controller_->consumeFrame(
                    item.streamId, cache.sequence);
                frame.has_value()) {
                QImage image = cache.converter.convert(*frame);
                if (!image.isNull()) {
                    cache.image = std::move(image);
                    cache.sequence = frame->sequence();
                    ++host_->statistics_.uploadedFrames;
                    if (const auto mailbox = host_->controller_->mailbox(
                            item.streamId);
                        mailbox != nullptr) {
                        mailbox->recordUploaded();
                    }
                    latestAge = std::max<qint64>(
                        0, monotonicMilliseconds() - frame->receivedMonotonicMs()
                    );
                } else {
                    ++host_->statistics_.unsupportedFrames;
                }
            }
            if (cache.image.isNull()) {
                continue;
            }
            const VideoPlacement placement = calculateVideoPlacement(
                item.videoViewport,
                cache.image.size(),
                item.displayMode
            );
            const QRectF source(
                placement.sourceUv.x() * cache.image.width(),
                placement.sourceUv.y() * cache.image.height(),
                placement.sourceUv.width() * cache.image.width(),
                placement.sourceUv.height() * cache.image.height()
            );
            painter.drawImage(placement.targetRect, cache.image, source);
            if (const auto mailbox = host_->controller_->mailbox(item.streamId);
                mailbox != nullptr) {
                mailbox->recordRendered();
            }
            ++host_->statistics_.renderedFrames;
        }
        for (auto iterator = caches_.begin(); iterator != caches_.end();) {
            iterator = activeStreams.find(iterator->first) == activeStreams.end()
                           ? caches_.erase(iterator)
                           : std::next(iterator);
        }
        ++host_->statistics_.paintCalls;
        host_->statistics_.lastPaintCpuUs = timer.nsecsElapsed() / 1000;
        host_->statistics_.lastUploadCpuUs = 0;
        host_->statistics_.lastGpuTimeUs = -1;
        host_->statistics_.latestFrameAgeMs = latestAge;
        host_->statistics_.textureBytes = 0;
        host_->onSurfacePainted();
    }

private:
    VideoCanvasHost *host_ = nullptr;
    std::unordered_map<StreamId, CpuFrameCache> caches_;
};

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
    delete openGLCanvas_;
    openGLCanvas_ = nullptr;
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
    if (openGLCanvas_ != nullptr) {
        openGLCanvas_->hide();
        delete openGLCanvas_;
        openGLCanvas_ = nullptr;
    }
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
    if (!cpuActive_ && openGLCanvas_ != nullptr &&
        openGLCanvas_->isValid()) {
        image = openGLCanvas_->grabFramebuffer();
        openGlFramebuffer = true;
    } else if (cpuCanvas_ != nullptr) {
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
    if (openGLCanvas_ != nullptr) {
        openGLCanvas_->setGeometry(rect());
    }
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
    openGLCanvas_ = new VideoOpenGLCanvas(this, this);
    openGLCanvas_->setGeometry(rect());
    openGLCanvas_->show();
    openGLCanvas_->lower();
    cpuActive_ = false;
    controller_->markDirty(RenderDirtyFlag::Resource);
}

void VideoCanvasHost::activateCpuFallback(const QString &reason)
{
    if (openGLCanvas_ != nullptr) {
        openGLCanvas_->hide();
    }
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
    } else if (openGLCanvas_ != nullptr) {
        openGLCanvas_->update();
    } else {
        paintPending_ = false;
    }
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
