#pragma once

#include <QString>

/**
 * @brief Actual, measured GL capability facts of one created Context.
 *
 * Every field must come from the real QPA/QOpenGLContext/glGet* result,
 * never from the requested QSurfaceFormat or the chip model. This struct
 * contains no GL types so it stays usable in CPU-only (RASTER) builds.
 */
struct EmbeddedGlCapabilities
{
    QString qpaPlatform;
    bool isOpenGles = false;
    int actualMajor = 0;
    int actualMinor = 0;
    QString vendor;
    QString renderer;
    QString version;
    int maxTextureSize = 0;
    int maxCombinedTextureUnits = 0;
    bool supportsRequiredRedRgTextures = false;
    bool supportsRequiredUnpackRowLength = false;
    bool framebufferComplete = false;
    bool shaderSmokePassed = false;
};

struct EmbeddedGlQualification
{
    bool qualified = false;
    /** Human-readable, diagnosable failure reason; empty when qualified. */
    QString reason;
};

/**
 * @brief Pure qualification decision for the production YUV renderer.
 *
 * Encodes only the ES3-core/Desktop-3.3 requirements of OpenGLGridRenderer;
 * vendor extensions are never required. Performs no GL calls and no
 * benchmark, so it is unit-testable on any platform.
 */
[[nodiscard]] EmbeddedGlQualification qualifyEmbeddedGlCapabilities(
    const EmbeddedGlCapabilities &capabilities
);
