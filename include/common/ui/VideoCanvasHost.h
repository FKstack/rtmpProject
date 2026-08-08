#pragma once

#include <QWidget>
#include <QImage>

#include <memory>

#include "render/RenderTypes.h"
#include "render/VideoRenderController.h"

class QTimer;
class QResizeEvent;
class QShowEvent;
class VideoOpenGLCanvas;
class CpuVideoCanvas;

enum class RendererPreference {
    Auto,
    OpenGL,
    Cpu,
};

/**
 * @brief Selects one OpenGL compositor or the diagnostic CPU compositor.
 *
 * The host itself owns no GL handles. Context-bound resources remain in the
 * OpenGL child and are released from that child's context lifecycle callbacks.
 */
class VideoCanvasHost final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoCanvasHost(
        RendererPreference preference = RendererPreference::Cpu,
        QWidget *parent = nullptr
    );
    ~VideoCanvasHost() override;

    VideoRenderController *controller() noexcept;
    const VideoRenderController *controller() const noexcept;

    void setSnapshot(RenderSnapshot snapshot);
    void registerStream(
        StreamId streamId,
        std::shared_ptr<LatestFrameMailbox> mailbox
    );
    void unregisterStream(StreamId streamId);
    void setTargetFps(int fps);
    void setRendererPreference(RendererPreference preference);

    [[nodiscard]] RendererPreference rendererPreference() const noexcept;
    [[nodiscard]] QString activeBackendName() const;
    [[nodiscard]] RenderStatistics statistics() const noexcept;
    [[nodiscard]] RenderRuntimeMetrics runtimeMetrics() const;
    [[nodiscard]] QImage grabFramebufferImage();

signals:
    void backendChanged(const QString &backendName);
    void renderingError(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    friend class VideoOpenGLCanvas;
    friend class CpuVideoCanvas;

    void createBackend();
    void activateCpuFallback(const QString &reason);
    void scheduleTick();
    void onSurfacePainted();
    void onOpenGLInitialized(
        bool success,
        const QString &error,
        const QString &graphicsApi = {},
        const QString &vendor = {},
        const QString &renderer = {},
        const QString &version = {}
    );

    RendererPreference preference_ = RendererPreference::Auto;
    std::unique_ptr<VideoRenderController> controller_;
    VideoOpenGLCanvas *openGLCanvas_ = nullptr;
    CpuVideoCanvas *cpuCanvas_ = nullptr;
    QTimer *scheduleTimer_ = nullptr;
    RenderStatistics statistics_;
    bool paintPending_ = false;
    bool cpuActive_ = false;
    bool fallbackOccurred_ = false;
    int targetFps_ = 15;
    qint64 lastPaintMonotonicMs_ = 0;
    QString fallbackReason_;
    QString graphicsApi_;
    QString openGlVendor_;
    QString openGlRenderer_;
    QString openGlVersion_;
};
