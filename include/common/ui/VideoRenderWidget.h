#pragma once

#include <cstdint>
#include <memory>

#include <QImage>
#include <QMetaObject>
#include <QOpenGLWidget>
#include <QString>

class QOpenGLTexture;
class QOpenGLTextureBlitter;

/**
 * @brief 使用 OpenGL 纹理显示 RGB/RGBA QImage 的可选渲染原型。
 *
 * 该控件不属于当前生产视频显示链路。它用于验证 Qt OpenGL 环境和后续渲染器
 * 抽象的可行性，不负责 FFmpeg 解码、YUV 转换或跨线程帧队列。
 *
 * @thread 只能在所属 Qt UI 线程中创建和调用。
 */
class VideoRenderWidget final : public QOpenGLWidget
{
    Q_OBJECT

public:
    /** @brief 创建 OpenGL 视频渲染原型，所有权由 parent 管理。 */
    explicit VideoRenderWidget(QWidget *parent = nullptr);
    ~VideoRenderWidget() override;

public slots:
    /**
     * @brief 保存一张隐式共享图像，并在下一次 OpenGL 绘制时上传纹理。
     *
     * @param image 非空 RGB/RGBA 图像；空图像不会替换当前画面。
     * @thread 必须在所属 Qt UI 线程调用。
     */
    void setFrame(const QImage &image);

    /** @brief 释放当前纹理并恢复黑色画面。 */
    void clearFrame();

signals:
    /**
     * @brief OpenGL 上下文和纹理绘制辅助器初始化完成。
     *
     * @param success 初始化是否成功。
     * @param vendor OpenGL 实现厂商。
     * @param renderer 当前渲染器。
     * @param version OpenGL 或 OpenGL ES 版本字符串。
     */
    void openGLInitialized(
        bool success,
        const QString &vendor,
        const QString &renderer,
        const QString &version
    );

    /** @brief 已完成一帧纹理绘制，用于原型诊断和冒烟测试。 */
    void frameRendered();

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    void cleanupOpenGLResources();
    void uploadPendingFrame();

    QImage frame_;
    std::unique_ptr<QOpenGLTexture> texture_;
    std::unique_ptr<QOpenGLTextureBlitter> blitter_;
    QMetaObject::Connection contextDestructionConnection_;
    std::uint64_t frameRevision_ = 0;
    std::uint64_t uploadedRevision_ = 0;
};
