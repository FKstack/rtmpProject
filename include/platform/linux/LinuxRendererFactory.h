#pragma once

#include "linux/LinuxRenderingPolicy.h"
#include "ui/VideoCanvasHost.h"

/**
 * @brief Maps a Linux policy decision to the canvas backend VideoCanvasHost creates.
 *
 * Holds no stream business state. The actual CPU/GL canvas widgets are owned
 * by VideoCanvasHost; this factory only resolves which backend the host must
 * use so Linux policy stays out of the shared UI classes.
 */
class LinuxRendererFactory
{
public:
    /** @brief True when this build compiled the GL canvas (RTMP_MONITOR_HAS_OPENGL). */
    [[nodiscard]] static bool isOpenGlBackendCompiled() noexcept;

    /**
     * @brief RendererPreference the canvas host must use for this decision.
     *
     * A CPU decision is forced to RendererPreference::Cpu so the host never
     * attempts GL on linuxfb or GL-less builds; a GLES3 decision preserves the
     * user's auto/opengl intent so the host keeps its runtime fallback path.
     */
    [[nodiscard]] static RendererPreference rendererPreferenceFor(
        const QString &requestedRenderer,
        const LinuxRenderingDecision &decision
    );
};
