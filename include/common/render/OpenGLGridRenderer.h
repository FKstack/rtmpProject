#pragma once

#include <QSize>
#include <QString>

#include <cstdint>
#include <memory>

#include "render/RenderTypes.h"

class QOpenGLExtraFunctions;
class VideoRenderController;

/** @brief Context-thread-only YUV compositor for one Qt OpenGL canvas. */
class OpenGLGridRenderer final
{
public:
    OpenGLGridRenderer();
    ~OpenGLGridRenderer();

    OpenGLGridRenderer(const OpenGLGridRenderer &) = delete;
    OpenGLGridRenderer &operator=(const OpenGLGridRenderer &) = delete;

    bool initialize(QOpenGLExtraFunctions *functions, bool openGles, QString *error);
    void release(QOpenGLExtraFunctions *functions) noexcept;
    bool render(
        QOpenGLExtraFunctions *functions,
        const QSize &framebufferSize,
        VideoRenderController *controller,
        RenderStatistics *statistics,
        QString *error
    );

    [[nodiscard]] bool isInitialized() const noexcept;

    /**
     * @brief 设置非 Snapshot 流纹理的保留策略；默认 KeepRegisteredStreams。
     *
     * 必须在 Context 线程调用（initialize/render 同一线程规则）。
     */
    void setTextureRetentionPolicy(TextureRetentionPolicy policy) noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
