#include "ui/VideoRenderWidget.h"

#include <algorithm>
#include <utility>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLTextureBlitter>
#include <QThread>

namespace {

QString openGLString(QOpenGLFunctions *functions, GLenum name)
{
    const GLubyte *value = functions->glGetString(name);
    if (value == nullptr) {
        return {};
    }
    return QString::fromLatin1(reinterpret_cast<const char *>(value));
}

} // namespace

VideoRenderWidget::VideoRenderWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(160, 90);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

VideoRenderWidget::~VideoRenderWidget()
{
    QObject::disconnect(contextDestructionConnection_);
    cleanupOpenGLResources();
}

void VideoRenderWidget::setFrame(const QImage &image)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (image.isNull()) {
        return;
    }

    frame_ = image;
    ++frameRevision_;
    update();
}

void VideoRenderWidget::clearFrame()
{
    Q_ASSERT(QThread::currentThread() == thread());
    frame_ = {};
    ++frameRevision_;
    update();
}

void VideoRenderWidget::initializeGL()
{
    QOpenGLContext *openGLContext = context();
    if (openGLContext == nullptr) {
        emit openGLInitialized(false, {}, {}, {});
        return;
    }

    QObject::disconnect(contextDestructionConnection_);
    contextDestructionConnection_ = connect(
        openGLContext,
        &QOpenGLContext::aboutToBeDestroyed,
        this,
        &VideoRenderWidget::cleanupOpenGLResources,
        Qt::DirectConnection
    );

    QOpenGLFunctions *functions = openGLContext->functions();
    functions->initializeOpenGLFunctions();

    blitter_ = std::make_unique<QOpenGLTextureBlitter>();
    const bool initialized = blitter_->create();
    emit openGLInitialized(
        initialized,
        openGLString(functions, GL_VENDOR),
        openGLString(functions, GL_RENDERER),
        openGLString(functions, GL_VERSION)
    );
}

void VideoRenderWidget::paintGL()
{
    QOpenGLContext *openGLContext = context();
    if (openGLContext == nullptr) {
        return;
    }

    QOpenGLFunctions *functions = openGLContext->functions();
    functions->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    functions->glClear(GL_COLOR_BUFFER_BIT);

    uploadPendingFrame();
    if (frame_.isNull() || texture_ == nullptr || blitter_ == nullptr ||
        !blitter_->isCreated()) {
        return;
    }

    const qreal pixelRatio = devicePixelRatioF();
    const QSize viewportSize(
        std::max(1, qRound(width() * pixelRatio)),
        std::max(1, qRound(height() * pixelRatio))
    );
    QSize targetSize = frame_.size();
    targetSize.scale(viewportSize, Qt::KeepAspectRatio);
    const QRectF targetRect(
        (viewportSize.width() - targetSize.width()) / 2.0,
        (viewportSize.height() - targetSize.height()) / 2.0,
        targetSize.width(),
        targetSize.height()
    );

    blitter_->bind();
    blitter_->blit(
        texture_->textureId(),
        QOpenGLTextureBlitter::targetTransform(
            targetRect,
            QRect(QPoint(0, 0), viewportSize)
        ),
        QOpenGLTextureBlitter::OriginBottomLeft
    );
    blitter_->release();
    emit frameRendered();
}

void VideoRenderWidget::cleanupOpenGLResources()
{
    if (texture_ == nullptr && blitter_ == nullptr) {
        return;
    }

    QOpenGLContext *openGLContext = context();
    if (openGLContext == nullptr || !openGLContext->isValid()) {
        texture_.reset();
        blitter_.reset();
        uploadedRevision_ = 0;
        return;
    }

    makeCurrent();
    texture_.reset();
    if (blitter_ != nullptr && blitter_->isCreated()) {
        blitter_->destroy();
    }
    blitter_.reset();
    uploadedRevision_ = 0;
    doneCurrent();
}

void VideoRenderWidget::uploadPendingFrame()
{
    if (uploadedRevision_ == frameRevision_) {
        return;
    }

    texture_.reset();
    uploadedRevision_ = frameRevision_;
    if (frame_.isNull()) {
        return;
    }

    QImage uploadImage =
        frame_.convertToFormat(QImage::Format_RGBA8888).mirrored();
    texture_ = std::make_unique<QOpenGLTexture>(
        uploadImage,
        QOpenGLTexture::DontGenerateMipMaps
    );
    texture_->setMinificationFilter(QOpenGLTexture::Linear);
    texture_->setMagnificationFilter(QOpenGLTexture::Linear);
    texture_->setWrapMode(QOpenGLTexture::ClampToEdge);
}
