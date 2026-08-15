#include "linux/LinuxRenderingPolicy.h"

LinuxRenderingDecision LinuxRenderingPolicy::decide(
    bool buildHasOpenGl,
    const QString &requestedRenderer,
    const QString &qpaPlatform
)
{
    LinuxRenderingDecision decision;
    const QString qpa = qpaPlatform.trimmed().toLower();
    const bool eglfs = qpa == QStringLiteral("eglfs");
    const bool linuxfb = qpa == QStringLiteral("linuxfb");
    decision.singleGlTopLevelWindow = eglfs;

    if (requestedRenderer == QStringLiteral("cpu")) {
        decision.backend = LinuxRendererBackendChoice::Cpu;
        return decision;
    }

    if (!buildHasOpenGl) {
        decision.backend = LinuxRendererBackendChoice::Cpu;
        if (requestedRenderer == QStringLiteral("opengl")) {
            decision.fallbackOccurred = true;
            decision.reason = QStringLiteral(
                "This build was configured without an OpenGL backend "
                "(RASTER); --renderer=opengl cannot be honored."
            );
        } else {
            decision.reason = QStringLiteral(
                "This build was configured without an OpenGL backend "
                "(RASTER); using CPU."
            );
        }
        return decision;
    }

    if (linuxfb) {
        decision.backend = LinuxRendererBackendChoice::Cpu;
        if (requestedRenderer == QStringLiteral("opengl")) {
            decision.fallbackOccurred = true;
        }
        decision.reason = QStringLiteral(
            "QPA platform is linuxfb; no EGL/GLES3 path exists, using CPU."
        );
        return decision;
    }

    decision.backend = LinuxRendererBackendChoice::OpenGlEs3;
    return decision;
}
