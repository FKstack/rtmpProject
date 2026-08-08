#pragma once

#include <QOpenGLWidget>

#include "render/OpenGLGridRenderer.h"

class VideoCanvasHost;

/**
 * @brief Qt Context lifecycle shell owning the OpenGLGridRenderer for one canvas.
 *
 * All GL resource creation and destruction happens on this widget's Context
 * thread; the class performs no FFmpeg decoding and no platform policy.
 * This compilation unit is only built when the target contains an OpenGL
 * backend (RTMP_MONITOR_HAS_OPENGL).
 */
class VideoOpenGLCanvas final : public QOpenGLWidget
{
public:
    explicit VideoOpenGLCanvas(VideoCanvasHost *host, QWidget *parent);
    ~VideoOpenGLCanvas() override;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

private:
    void cleanup();

    VideoCanvasHost *host_ = nullptr;
    OpenGLGridRenderer renderer_;
    QMetaObject::Connection contextDestructionConnection_;
};
