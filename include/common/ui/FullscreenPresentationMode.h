#pragma once

#include <QString>

/**
 * @brief How single-stream fullscreen is presented on the current platform.
 *
 * EGLFS allows only one native/EGL top-level window, so fullscreen must reuse
 * the main canvas by switching its RenderSnapshot; desktop platforms keep the
 * existing temporary fullscreen window with its own non-shared canvas.
 */
enum class FullscreenPresentationMode {
    TemporaryWindowCanvas,
    ReuseMainCanvas,
};

[[nodiscard]] inline FullscreenPresentationMode fullscreenPresentationModeForQpa(
    const QString &qpaPlatform
) noexcept
{
    return qpaPlatform.compare(QStringLiteral("eglfs"), Qt::CaseInsensitive) ==
                   0
               ? FullscreenPresentationMode::ReuseMainCanvas
               : FullscreenPresentationMode::TemporaryWindowCanvas;
}
