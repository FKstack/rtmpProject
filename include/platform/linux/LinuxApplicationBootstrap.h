#pragma once

#include <QString>

struct LinuxBootstrapResult
{
    bool es3SurfaceFormatRequested = false;
    /** Diagnostic note explaining the choice; may be empty. */
    QString note;
};

/**
 * @brief Linux QPA/SurfaceFormat handling that must run before QApplication exists.
 *
 * Only the GLES3 path requests an OpenGL ES 3.0 default surface format;
 * RASTER builds, explicit --renderer=cpu and linuxfb targets leave the
 * default format untouched so Qt never initializes EGL. This class has no
 * OpenGL dependency and is safe to compile in CPU-only builds.
 */
class LinuxApplicationBootstrap
{
public:
    /**
     * @param buildHasOpenGl  RTMP_MONITOR_HAS_OPENGL for this build.
     * @param requestedRenderer  CLI value: "auto", "opengl" or "cpu" (lowercase).
     * @param argc/argv  Used to honor a `-platform` override before QApplication.
     */
    [[nodiscard]] static LinuxBootstrapResult configureSurfaceFormat(
        bool buildHasOpenGl,
        const QString &requestedRenderer,
        int argc,
        char *argv[]
    );

    /** @brief Effective pre-QApplication QPA candidate (QT_QPA_PLATFORM/-platform). */
    [[nodiscard]] static QString requestedQpaPlatform(int argc, char *argv[]);

    /**
     * @brief Pre-QApplication scan of `--renderer`; defaults to "auto".
     *
     * The full QCommandLineParser run happens after QApplication creation;
     * this scan exists only because the surface format must be chosen first.
     */
    [[nodiscard]] static QString requestedRendererFromArgs(int argc, char *argv[]);
};
