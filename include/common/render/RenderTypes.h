#pragma once

#include <QMatrix3x3>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector3D>

#include <atomic>
#include <cstdint>
#include <vector>

#include "media/PlaybackTypes.h"
#include "media/VideoFrame.h"

enum class VideoDisplayMode {
    Contain,
    Cover,
};

/**
 * @brief 纹理在流暂时离开 Snapshot 时的保留策略。
 *
 * KeepRegisteredStreams：已注册但暂时不在 Snapshot 的流保留纹理不继续上传，
 * 适用于 EGLFS 单画布全屏往返与内存充足设备；ReleaseImmediately：S0 等极低
 * 内存设备立即释放。Context lost 时两种策略都全部释放。
 */
enum class TextureRetentionPolicy {
    KeepRegisteredStreams,
    ReleaseImmediately,
};

enum class RenderDirtyFlag : std::uint32_t {
    None = 0,
    Frame = 1U << 0U,
    Layout = 1U << 1U,
    Overlay = 1U << 2U,
    Resource = 1U << 3U,
    Viewport = 1U << 4U,
    ColorMetadata = 1U << 5U,
};

using RenderDirtyFlags = std::uint32_t;

constexpr RenderDirtyFlags renderDirtyBit(RenderDirtyFlag flag) noexcept
{
    return static_cast<RenderDirtyFlags>(flag);
}

struct RenderItem
{
    StreamId streamId = kInvalidStreamId;
    QRectF tileRect;
    QRectF videoViewport;
    VideoDisplayMode displayMode = VideoDisplayMode::Contain;
    QString title;
    QString status;
    bool frameVisible = false;
    bool selected = false;
    bool fullscreen = false;
};

struct RenderSnapshot
{
    std::uint64_t generation = 0;
    QSize logicalCanvasSize;
    qreal devicePixelRatio = 1.0;
    std::vector<RenderItem> items;
};

struct VideoPlacement
{
    QRectF targetRect;
    QRectF sourceUv {0.0, 0.0, 1.0, 1.0};
};

struct YuvColorTransform
{
    QMatrix3x3 matrix;
    QVector3D offset;
};

struct RenderStatistics
{
    std::uint64_t scheduleChecks = 0;
    std::uint64_t updateRequests = 0;
    std::uint64_t dirtyMerges = 0;
    std::uint64_t paintCalls = 0;
    std::uint64_t uploadedFrames = 0;
    std::uint64_t renderedFrames = 0;
    std::uint64_t unsupportedFrames = 0;
    qint64 lastPaintCpuUs = 0;
    qint64 lastUploadCpuUs = 0;
    qint64 lastGpuTimeUs = -1;
    qint64 latestFrameAgeMs = -1;
    std::uint64_t textureBytes = 0;
};

class RenderDirtyState final
{
public:
    void mark(RenderDirtyFlag flag) noexcept;
    [[nodiscard]] RenderDirtyFlags pending() const noexcept;
    [[nodiscard]] RenderDirtyFlags consume() noexcept;
    [[nodiscard]] std::uint64_t mergeCount() const noexcept;

private:
    std::atomic_uint32_t flags_ {0};
    std::atomic_uint64_t mergeCount_ {0};
};

[[nodiscard]] VideoPlacement calculateVideoPlacement(
    const QRectF &viewport,
    const QSize &sourceSize,
    VideoDisplayMode mode
) noexcept;

[[nodiscard]] YuvColorTransform yuvColorTransform(
    const VideoColorDescription &description,
    int width,
    int height
) noexcept;
