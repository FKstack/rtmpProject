#include "linux/LinuxRendererFactory.h"

#include "RtmpMonitorBuildConfig.h"

bool LinuxRendererFactory::isOpenGlBackendCompiled() noexcept
{
    return RTMP_MONITOR_HAS_OPENGL != 0;
}

RendererPreference LinuxRendererFactory::rendererPreferenceFor(
    const QString &requestedRenderer,
    const LinuxRenderingDecision &decision
)
{
    if (decision.backend == LinuxRendererBackendChoice::Cpu) {
        return RendererPreference::Cpu;
    }
    return requestedRenderer == QStringLiteral("opengl")
               ? RendererPreference::OpenGL
               : RendererPreference::Auto;
}
