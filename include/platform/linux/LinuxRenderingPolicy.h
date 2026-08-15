#pragma once

#include <QString>

/**
 * @brief Linux renderer backend chosen by policy before any GL work happens.
 */
enum class LinuxRendererBackendChoice {
    Cpu,
    OpenGlEs3,
};

struct LinuxRenderingDecision
{
    LinuxRendererBackendChoice backend = LinuxRendererBackendChoice::Cpu;
    /** True when the user's explicit choice could not be honored. */
    bool fallbackOccurred = false;
    /** Diagnosable reason when CPU is chosen instead of a requested GL path. */
    QString reason;
    /** EGLFS allows only one GL top-level window; fullscreen must reuse the main canvas. */
    bool singleGlTopLevelWindow = false;
};

/**
 * @brief Decides the Linux CPU/GLES3 backend from build capability, CLI and QPA facts.
 *
 * Pure decision logic: no decoding, no painting, no benchmarking and no GL
 * calls. Context-level qualification happens later through
 * EmbeddedGlCapabilities once a real context exists.
 */
class LinuxRenderingPolicy
{
public:
    /**
     * @param buildHasOpenGl  RTMP_MONITOR_HAS_OPENGL for this build.
     * @param requestedRenderer  CLI value: "auto", "opengl" or "cpu" (lowercase).
     * @param qpaPlatform  Actual QPA name, e.g. QGuiApplication::platformName().
     */
    [[nodiscard]] static LinuxRenderingDecision decide(
        bool buildHasOpenGl,
        const QString &requestedRenderer,
        const QString &qpaPlatform
    );
};
