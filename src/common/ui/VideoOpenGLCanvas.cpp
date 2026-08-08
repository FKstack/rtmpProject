#include "ui/VideoOpenGLCanvas.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

#include <algorithm>

#include "render/EmbeddedGlCapabilities.h"
#include "ui/VideoCanvasHost.h"

namespace {

QString openGlString(QOpenGLExtraFunctions *functions, GLenum name)
{
    if (functions == nullptr) {
        return {};
    }
    const auto *value = functions->glGetString(name);
    return value == nullptr
               ? QString {}
               : QString::fromLatin1(reinterpret_cast<const char *>(value));
}

} // namespace

VideoOpenGLCanvas::VideoOpenGLCanvas(VideoCanvasHost *host, QWidget *parent)
    : QOpenGLWidget(parent)
    , host_(host)
{
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

VideoOpenGLCanvas::~VideoOpenGLCanvas()
{
    QObject::disconnect(contextDestructionConnection_);
    cleanup();
}

void VideoOpenGLCanvas::initializeGL()
{
    QOpenGLContext *openGLContext = context();
    if (openGLContext == nullptr || !openGLContext->isValid()) {
        host_->onOpenGLInitialized(
            false, QStringLiteral("OpenGL context creation failed.")
        );
        return;
    }
    const QSurfaceFormat format = openGLContext->format();
    const bool openGles = openGLContext->isOpenGLES();
    const bool versionReady = openGles
                                  ? format.majorVersion() >= 3
                                  : (format.majorVersion() > 3 ||
                                     (format.majorVersion() == 3 &&
                                      format.minorVersion() >= 3));

    QOpenGLExtraFunctions *functions = openGLContext->extraFunctions();
    functions->initializeOpenGLFunctions();

    // 只采信实际 Context 的事实，不使用请求值、不跑 benchmark、不做回读。
    EmbeddedGlCapabilities capabilities;
    capabilities.qpaPlatform = QGuiApplication::platformName();
    capabilities.isOpenGles = openGles;
    capabilities.actualMajor = format.majorVersion();
    capabilities.actualMinor = format.minorVersion();
    capabilities.vendor = openGlString(functions, GL_VENDOR);
    capabilities.renderer = openGlString(functions, GL_RENDERER);
    capabilities.version = openGlString(functions, GL_VERSION);

    if (!versionReady) {
        // 版本不满足时跳过 ES3-only 探测，避免调用不存在的函数指针。
        const EmbeddedGlQualification qualification =
            qualifyEmbeddedGlCapabilities(capabilities);
        host_->onOpenGLInitialized(
            false,
            qualification.reason.isEmpty()
                ? (openGles
                       ? QStringLiteral("OpenGL ES 3.0 or newer is required.")
                       : QStringLiteral(
                             "Desktop OpenGL 3.3 or newer is required."
                         ))
                : qualification.reason,
            openGles ? QStringLiteral("OpenGL ES")
                     : QStringLiteral("Desktop OpenGL"),
            capabilities.vendor,
            capabilities.renderer,
            capabilities.version
        );
        return;
    }

    GLint numericFact = 0;
    functions->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &numericFact);
    capabilities.maxTextureSize = numericFact;
    numericFact = 0;
    functions->glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &numericFact);
    capabilities.maxCombinedTextureUnits = numericFact;

    GLuint probeTexture = 0;
    functions->glGenTextures(1, &probeTexture);
    functions->glBindTexture(GL_TEXTURE_2D, probeTexture);
    functions->glTexImage2D(
        GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr
    );
    const bool r8Ready = functions->glGetError() == GL_NO_ERROR;
    functions->glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RG8, 1, 1, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr
    );
    const bool rg8Ready = functions->glGetError() == GL_NO_ERROR;
    capabilities.supportsRequiredRedRgTextures =
        probeTexture != 0 && r8Ready && rg8Ready;

    functions->glPixelStorei(GL_UNPACK_ROW_LENGTH, 1);
    capabilities.supportsRequiredUnpackRowLength =
        functions->glGetError() == GL_NO_ERROR;
    functions->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    GLuint probeFramebuffer = 0;
    functions->glGenFramebuffers(1, &probeFramebuffer);
    functions->glBindFramebuffer(GL_FRAMEBUFFER, probeFramebuffer);
    functions->glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        probeTexture,
        0
    );
    capabilities.framebufferComplete =
        functions->glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
        GL_FRAMEBUFFER_COMPLETE;
    functions->glBindFramebuffer(
        GL_FRAMEBUFFER, defaultFramebufferObject()
    );
    functions->glDeleteFramebuffers(1, &probeFramebuffer);
    functions->glBindTexture(GL_TEXTURE_2D, 0);
    functions->glDeleteTextures(1, &probeTexture);

    QObject::disconnect(contextDestructionConnection_);
    contextDestructionConnection_ = QObject::connect(
        openGLContext,
        &QOpenGLContext::aboutToBeDestroyed,
        this,
        [this] { cleanup(); },
        Qt::DirectConnection
    );

    QString shaderError;
    capabilities.shaderSmokePassed =
        renderer_.initialize(functions, openGles, &shaderError);

    const EmbeddedGlQualification qualification =
        qualifyEmbeddedGlCapabilities(capabilities);
    QString error = qualification.reason;
    if (!qualification.qualified && !shaderError.isEmpty() &&
        !capabilities.shaderSmokePassed) {
        error = QStringLiteral("%1 %2").arg(error, shaderError).trimmed();
    }
    host_->onOpenGLInitialized(
        qualification.qualified,
        error,
        openGles ? QStringLiteral("OpenGL ES")
                 : QStringLiteral("Desktop OpenGL"),
        capabilities.vendor,
        capabilities.renderer,
        capabilities.version
    );
}

void VideoOpenGLCanvas::resizeGL(int, int)
{
    host_->controller_->markDirty(RenderDirtyFlag::Viewport);
}

void VideoOpenGLCanvas::paintGL()
{
    (void)host_->controller_->consumeDirty();
    QString error;
    QOpenGLContext *openGLContext = context();
    const qreal dpr = devicePixelRatioF();
    const QSize framebufferSize(
        std::max(1, qRound(width() * dpr)),
        std::max(1, qRound(height() * dpr))
    );
    const bool rendered = openGLContext != nullptr && renderer_.render(
        openGLContext->extraFunctions(),
        framebufferSize,
        host_->controller_.get(),
        &host_->statistics_,
        &error
    );
    if (!rendered && !error.isEmpty()) {
        emit host_->renderingError(error);
    }
    host_->onSurfacePainted();
}

void VideoOpenGLCanvas::cleanup()
{
    QOpenGLContext *openGLContext = context();
    if (!renderer_.isInitialized()) {
        return;
    }
    if (openGLContext == nullptr || !openGLContext->isValid()) {
        renderer_.release(nullptr);
        host_->controller_->markDirty(RenderDirtyFlag::Resource);
        return;
    }
    makeCurrent();
    renderer_.release(openGLContext->extraFunctions());
    doneCurrent();
    host_->controller_->markDirty(RenderDirtyFlag::Resource);
}
