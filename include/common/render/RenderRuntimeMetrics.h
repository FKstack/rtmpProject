#pragma once

#include <QString>

#include <cstdint>

/**
 * @brief Runtime renderer identity and one-canvas counters for metrics schema v4.
 *
 * This contract belongs to the render layer. Diagnostics may aggregate it
 * with media metrics, while the media layer remains unaware of render state.
 */
struct RenderRuntimeMetrics
{
    QString requestedBackend = QStringLiteral("cpu");
    QString activeBackend = QStringLiteral("unknown");
    bool fallbackOccurred = false;
    QString fallbackReason;
    QString requestedDisplayFps = QStringLiteral("auto");
    int effectiveDisplayFps = 30;
    QString graphicsApi;
    QString openGlVendor;
    QString openGlRenderer;
    QString openGlVersion;
    std::uint64_t scheduleChecks = 0;
    std::uint64_t deadlineMisses = 0;
    std::uint64_t updateRequests = 0;
    std::uint64_t dirtyMerges = 0;
    std::uint64_t paintCalls = 0;
    std::uint64_t uploadedFrames = 0;
    std::uint64_t renderedFrames = 0;
    std::uint64_t unsupportedFrames = 0;
    qint64 paintCpuUs = 0;
    qint64 uploadCpuUs = 0;
    qint64 gpuTimeUs = -1;
    qint64 paintCpuP95Us = -1;
    qint64 uploadCpuP95Us = -1;
    qint64 gpuTimeP95Us = -1;
    qint64 latestFrameAgeMs = -1;
    qint64 textureBytes = 0;
    int renderItemCount = 0;
    int visibleRenderItemCount = 0;
    int boundMailboxCount = 0;
};
