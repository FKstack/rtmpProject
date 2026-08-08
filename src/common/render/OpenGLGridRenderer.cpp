#include "render/OpenGLGridRenderer.h"

#include <QElapsedTimer>
#include <QMatrix3x3>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "render/VideoRenderController.h"

namespace {

constexpr std::array<float, 16> kQuadVertices {
    -1.0F, -1.0F, 0.0F, 1.0F,
     1.0F, -1.0F, 1.0F, 1.0F,
    -1.0F,  1.0F, 0.0F, 0.0F,
     1.0F,  1.0F, 1.0F, 0.0F,
};

const char *desktopVertexShader()
{
    return R"GLSL(#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
out vec2 uv;
uniform vec4 uvRect;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = uvRect.xy + texCoord * uvRect.zw;
}
)GLSL";
}

const char *desktopFragmentShader()
{
    return R"GLSL(#version 330 core
in vec2 uv;
out vec4 fragmentColor;
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;
uniform sampler2D uvTexture;
uniform bool nv12;
uniform mat3 yuvMatrix;
uniform vec3 yuvOffset;
void main() {
    float y = texture(yTexture, uv).r;
    vec2 chroma = nv12
        ? texture(uvTexture, uv).rg
        : vec2(texture(uTexture, uv).r, texture(vTexture, uv).r);
    vec3 rgb = yuvMatrix * (vec3(y, chroma) + yuvOffset);
    fragmentColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)GLSL";
}

const char *esVertexShader()
{
    return R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
out vec2 uv;
uniform vec4 uvRect;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = uvRect.xy + texCoord * uvRect.zw;
}
)GLSL";
}

const char *esFragmentShader()
{
    return R"GLSL(#version 300 es
precision highp float;
in vec2 uv;
out vec4 fragmentColor;
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;
uniform sampler2D uvTexture;
uniform bool nv12;
uniform mat3 yuvMatrix;
uniform vec3 yuvOffset;
void main() {
    float y = texture(yTexture, uv).r;
    vec2 chroma = nv12
        ? texture(uvTexture, uv).rg
        : vec2(texture(uTexture, uv).r, texture(vTexture, uv).r);
    vec3 rgb = yuvMatrix * (vec3(y, chroma) + yuvOffset);
    fragmentColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)GLSL";
}

int planeWidth(const VideoFrame &frame, int index)
{
    if (index == 0) {
        return frame.width();
    }
    return (frame.width() + 1) / 2;
}

int planeHeight(const VideoFrame &frame, int index)
{
    return index == 0 ? frame.height() : (frame.height() + 1) / 2;
}

int bytesPerPixel(const VideoFrame &frame, int index)
{
    return frame.pixelFormat() == VideoPixelFormat::Nv12_8 && index == 1 ? 2 : 1;
}

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           )
        .count();
}

} // namespace

struct OpenGLGridRenderer::Implementation
{
    struct TextureSet
    {
        std::array<GLuint, VideoFrame::kMaximumPlanes> textures {};
        std::array<std::vector<std::uint8_t>, VideoFrame::kMaximumPlanes> staging;
        VideoPixelFormat format = VideoPixelFormat::Yuv420P8;
        int width = 0;
        int height = 0;
        int planeCount = 0;
        std::uint64_t sequence = 0;
        std::uint64_t bytes = 0;
        VideoColorDescription color;
    };

    std::unique_ptr<QOpenGLShaderProgram> program;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    std::array<GLuint, 2> gpuQueries {};
    std::array<bool, 2> gpuQueryInFlight {};
    int nextGpuQuery = 0;
    bool gpuTimingSupported = false;
    qint64 lastGpuTimeUs = -1;
    bool initialized = false;
    std::unordered_map<StreamId, TextureSet> textures;

    void releaseTextureSet(QOpenGLExtraFunctions *functions, TextureSet &set)
    {
        if (set.planeCount > 0) {
            functions->glDeleteTextures(set.planeCount, set.textures.data());
        }
        set = {};
    }

    void releaseAll(QOpenGLExtraFunctions *functions)
    {
        for (auto &entry : textures) {
            releaseTextureSet(functions, entry.second);
        }
        textures.clear();
    }

    bool allocateTextures(
        QOpenGLExtraFunctions *functions,
        TextureSet &set,
        const VideoFrame &frame
    )
    {
        releaseTextureSet(functions, set);
        set.format = frame.pixelFormat();
        set.width = frame.width();
        set.height = frame.height();
        set.planeCount = frame.planeCount();
        functions->glGenTextures(set.planeCount, set.textures.data());
        if (set.textures[0] == 0) {
            releaseTextureSet(functions, set);
            return false;
        }
        for (int index = 0; index < set.planeCount; ++index) {
            const int pixelBytes = bytesPerPixel(frame, index);
            const GLenum internalFormat = pixelBytes == 2 ? GL_RG8 : GL_R8;
            const GLenum uploadFormat = pixelBytes == 2 ? GL_RG : GL_RED;
            functions->glBindTexture(GL_TEXTURE_2D, set.textures[index]);
            functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            functions->glTexImage2D(
                GL_TEXTURE_2D,
                0,
                internalFormat,
                planeWidth(frame, index),
                planeHeight(frame, index),
                0,
                uploadFormat,
                GL_UNSIGNED_BYTE,
                nullptr
            );
            set.bytes += static_cast<std::uint64_t>(planeWidth(frame, index)) *
                         planeHeight(frame, index) * pixelBytes;
        }
        functions->glBindTexture(GL_TEXTURE_2D, 0);
        return functions->glGetError() == GL_NO_ERROR;
    }

    bool uploadPlane(
        QOpenGLExtraFunctions *functions,
        TextureSet &set,
        const VideoFrame &frame,
        int index
    )
    {
        const VideoPlaneView plane = frame.plane(index);
        const int width = planeWidth(frame, index);
        const int height = planeHeight(frame, index);
        const int pixelBytes = bytesPerPixel(frame, index);
        const GLenum format = pixelBytes == 2 ? GL_RG : GL_RED;
        const std::uint8_t *data = plane.data;
        int rowLength = 0;
        if (plane.stride > 0 && plane.stride % pixelBytes == 0) {
            rowLength = static_cast<int>(plane.stride / pixelBytes);
        } else {
            auto &staging = set.staging[index];
            staging.resize(static_cast<std::size_t>(plane.rowBytes) * height);
            const std::uint8_t *source = plane.data;
            for (int row = 0; row < height; ++row) {
                std::memcpy(
                    staging.data() + static_cast<std::size_t>(row) * plane.rowBytes,
                    source,
                    static_cast<std::size_t>(plane.rowBytes)
                );
                source += plane.stride;
            }
            data = staging.data();
            rowLength = plane.rowBytes / pixelBytes;
        }

        functions->glActiveTexture(GL_TEXTURE0 + index);
        functions->glBindTexture(GL_TEXTURE_2D, set.textures[index]);
        functions->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        functions->glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
        functions->glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            width,
            height,
            format,
            GL_UNSIGNED_BYTE,
            data
        );
        functions->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        return functions->glGetError() == GL_NO_ERROR;
    }
};

OpenGLGridRenderer::OpenGLGridRenderer()
    : implementation_(std::make_unique<Implementation>())
{
}

OpenGLGridRenderer::~OpenGLGridRenderer() = default;

bool OpenGLGridRenderer::initialize(
    QOpenGLExtraFunctions *functions,
    bool openGles,
    QString *error
)
{
    if (functions == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("OpenGL functions are unavailable.");
        }
        return false;
    }
    release(functions);
    functions->initializeOpenGLFunctions();

    auto program = std::make_unique<QOpenGLShaderProgram>();
    const bool vertexReady = program->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        openGles ? esVertexShader() : desktopVertexShader()
    );
    const bool fragmentReady = program->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        openGles ? esFragmentShader() : desktopFragmentShader()
    );
    if (!vertexReady || !fragmentReady || !program->link()) {
        if (error != nullptr) {
            *error = program->log();
        }
        return false;
    }

    functions->glGenVertexArrays(1, &implementation_->vertexArray);
    functions->glGenBuffers(1, &implementation_->vertexBuffer);
    functions->glBindVertexArray(implementation_->vertexArray);
    functions->glBindBuffer(GL_ARRAY_BUFFER, implementation_->vertexBuffer);
    functions->glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(sizeof(kQuadVertices)),
        kQuadVertices.data(),
        GL_STATIC_DRAW
    );
    functions->glEnableVertexAttribArray(0);
    functions->glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr
    );
    functions->glEnableVertexAttribArray(1);
    functions->glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<const void *>(2 * sizeof(float))
    );
    functions->glBindVertexArray(0);
    functions->glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (functions->glGetError() != GL_NO_ERROR) {
        if (error != nullptr) {
            *error = QStringLiteral("Unable to initialize OpenGL quad resources.");
        }
        release(functions);
        return false;
    }

    if (!openGles) {
        functions->glGenQueries(
            static_cast<GLsizei>(implementation_->gpuQueries.size()),
            implementation_->gpuQueries.data()
        );
        implementation_->gpuTimingSupported =
            implementation_->gpuQueries[0] != 0 &&
            implementation_->gpuQueries[1] != 0 &&
            functions->glGetError() == GL_NO_ERROR;
        if (!implementation_->gpuTimingSupported) {
            if (implementation_->gpuQueries[0] != 0 ||
                implementation_->gpuQueries[1] != 0) {
                functions->glDeleteQueries(
                    static_cast<GLsizei>(implementation_->gpuQueries.size()),
                    implementation_->gpuQueries.data()
                );
            }
            implementation_->gpuQueries = {};
            while (functions->glGetError() != GL_NO_ERROR) {
            }
        }
    }

    implementation_->program = std::move(program);
    implementation_->initialized = true;
    return true;
}

void OpenGLGridRenderer::release(QOpenGLExtraFunctions *functions) noexcept
{
    if (functions == nullptr) {
        implementation_ = std::make_unique<Implementation>();
        return;
    }
    implementation_->releaseAll(functions);
    if (implementation_->gpuQueries[0] != 0 ||
        implementation_->gpuQueries[1] != 0) {
        functions->glDeleteQueries(
            static_cast<GLsizei>(implementation_->gpuQueries.size()),
            implementation_->gpuQueries.data()
        );
    }
    if (implementation_->vertexBuffer != 0) {
        functions->glDeleteBuffers(1, &implementation_->vertexBuffer);
    }
    if (implementation_->vertexArray != 0) {
        functions->glDeleteVertexArrays(1, &implementation_->vertexArray);
    }
    implementation_->program.reset();
    implementation_->vertexArray = 0;
    implementation_->vertexBuffer = 0;
    implementation_->gpuQueries = {};
    implementation_->gpuQueryInFlight = {};
    implementation_->gpuTimingSupported = false;
    implementation_->lastGpuTimeUs = -1;
    implementation_->initialized = false;
}

bool OpenGLGridRenderer::render(
    QOpenGLExtraFunctions *functions,
    const QSize &framebufferSize,
    VideoRenderController *controller,
    RenderStatistics *statistics,
    QString *error
)
{
    if (!isInitialized() || functions == nullptr || controller == nullptr ||
        framebufferSize.isEmpty()) {
        return false;
    }

    QElapsedTimer paintTimer;
    paintTimer.start();
    if (implementation_->gpuTimingSupported) {
        for (std::size_t index = 0;
             index < implementation_->gpuQueries.size();
             ++index) {
            if (!implementation_->gpuQueryInFlight[index]) {
                continue;
            }
            GLuint available = GL_FALSE;
            functions->glGetQueryObjectuiv(
                implementation_->gpuQueries[index],
                GL_QUERY_RESULT_AVAILABLE,
                &available
            );
            if (available == GL_TRUE) {
                GLuint elapsedNanoseconds = 0;
                functions->glGetQueryObjectuiv(
                    implementation_->gpuQueries[index],
                    GL_QUERY_RESULT,
                    &elapsedNanoseconds
                );
                implementation_->lastGpuTimeUs =
                    static_cast<qint64>(elapsedNanoseconds / 1000U);
                implementation_->gpuQueryInFlight[index] = false;
            }
        }
    }
    int activeGpuQuery = -1;
    if (implementation_->gpuTimingSupported &&
        !implementation_->gpuQueryInFlight[implementation_->nextGpuQuery]) {
        activeGpuQuery = implementation_->nextGpuQuery;
        functions->glBeginQuery(
            GL_TIME_ELAPSED,
            implementation_->gpuQueries[activeGpuQuery]
        );
    }
    functions->glDisable(GL_DEPTH_TEST);
    functions->glDisable(GL_BLEND);
    functions->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    functions->glClear(GL_COLOR_BUFFER_BIT);

    auto &program = *implementation_->program;
    program.bind();
    program.setUniformValue("yTexture", 0);
    program.setUniformValue("uTexture", 1);
    program.setUniformValue("vTexture", 2);
    program.setUniformValue("uvTexture", 1);
    functions->glBindVertexArray(implementation_->vertexArray);
    functions->glEnable(GL_SCISSOR_TEST);

    const RenderSnapshot &snapshot = controller->snapshot();
    std::unordered_set<StreamId> activeStreams;
    activeStreams.reserve(snapshot.items.size());
    for (const RenderItem &item : snapshot.items) {
        if (item.streamId != kInvalidStreamId) {
            activeStreams.insert(item.streamId);
        }
    }
    for (auto iterator = implementation_->textures.begin();
         iterator != implementation_->textures.end();) {
        if (activeStreams.find(iterator->first) == activeStreams.end()) {
            implementation_->releaseTextureSet(functions, iterator->second);
            iterator = implementation_->textures.erase(iterator);
        } else {
            ++iterator;
        }
    }
    qint64 uploadMicroseconds = 0;
    std::uint64_t textureBytes = 0;
    for (const RenderItem &item : snapshot.items) {
        if (item.streamId == kInvalidStreamId || !item.frameVisible) {
            continue;
        }
        auto &textureSet = implementation_->textures[item.streamId];
        const auto frame = controller->consumeFrame(
            item.streamId, textureSet.sequence
        );
        if (frame.has_value()) {
            if (!isSupportedSdrTransfer(frame->color().transfer)) {
                if (statistics != nullptr) {
                    ++statistics->unsupportedFrames;
                }
                continue;
            }
            const bool allocationRequired =
                textureSet.width != frame->width() ||
                textureSet.height != frame->height() ||
                textureSet.format != frame->pixelFormat() ||
                textureSet.planeCount != frame->planeCount();
            if (allocationRequired && !implementation_->allocateTextures(
                    functions, textureSet, *frame)) {
                if (error != nullptr) {
                    *error = QStringLiteral("Unable to allocate YUV textures for stream %1.")
                                 .arg(item.streamId);
                }
                continue;
            }
            QElapsedTimer uploadTimer;
            uploadTimer.start();
            bool uploaded = true;
            for (int index = 0; index < frame->planeCount(); ++index) {
                uploaded = implementation_->uploadPlane(
                               functions, textureSet, *frame, index
                           ) && uploaded;
            }
            uploadMicroseconds += uploadTimer.nsecsElapsed() / 1000;
            if (!uploaded) {
                if (error != nullptr) {
                    *error = QStringLiteral("YUV texture upload failed for stream %1.")
                                 .arg(item.streamId);
                }
                continue;
            }
            textureSet.sequence = frame->sequence();
            textureSet.color = frame->color();
            if (statistics != nullptr) {
                ++statistics->uploadedFrames;
                const qint64 age = std::max<qint64>(
                    0,
                    monotonicMilliseconds() - frame->receivedMonotonicMs()
                );
                statistics->latestFrameAgeMs = age;
            }
            if (const auto mailbox = controller->mailbox(item.streamId);
                mailbox != nullptr) {
                mailbox->recordUploaded();
            }
        }
        if (textureSet.sequence == 0 || textureSet.textures[0] == 0) {
            continue;
        }

        const VideoPlacement placement = calculateVideoPlacement(
            item.videoViewport,
            QSize(textureSet.width, textureSet.height),
            item.displayMode
        );
        if (placement.targetRect.isEmpty()) {
            continue;
        }
        const qreal dpr = std::max<qreal>(1.0, snapshot.devicePixelRatio);
        const int x = qRound(placement.targetRect.x() * dpr);
        const int yTop = qRound(placement.targetRect.y() * dpr);
        const int width = std::max(1, qRound(placement.targetRect.width() * dpr));
        const int height = std::max(1, qRound(placement.targetRect.height() * dpr));
        const int y = framebufferSize.height() - yTop - height;
        functions->glViewport(x, y, width, height);
        functions->glScissor(x, y, width, height);

        const bool nv12 = textureSet.format == VideoPixelFormat::Nv12_8;
        program.setUniformValue("nv12", nv12);
        const YuvColorTransform transform = yuvColorTransform(
            textureSet.color,
            textureSet.width,
            textureSet.height
        );
        program.setUniformValue("yuvMatrix", transform.matrix);
        program.setUniformValue("yuvOffset", transform.offset);
        program.setUniformValue(
            "uvRect",
            QVector4D(
                placement.sourceUv.x(),
                placement.sourceUv.y(),
                placement.sourceUv.width(),
                placement.sourceUv.height()
            )
        );
        for (int index = 0; index < textureSet.planeCount; ++index) {
            functions->glActiveTexture(GL_TEXTURE0 + index);
            functions->glBindTexture(GL_TEXTURE_2D, textureSet.textures[index]);
        }
        functions->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (const auto mailbox = controller->mailbox(item.streamId);
            mailbox != nullptr) {
            mailbox->recordRendered();
        }
        if (statistics != nullptr) {
            ++statistics->renderedFrames;
        }
        textureBytes += textureSet.bytes;
    }

    functions->glDisable(GL_SCISSOR_TEST);
    functions->glBindVertexArray(0);
    program.release();
    if (activeGpuQuery >= 0) {
        functions->glEndQuery(GL_TIME_ELAPSED);
        implementation_->gpuQueryInFlight[activeGpuQuery] = true;
        implementation_->nextGpuQuery =
            (activeGpuQuery + 1) %
            static_cast<int>(implementation_->gpuQueries.size());
    }
    if (statistics != nullptr) {
        ++statistics->paintCalls;
        statistics->lastPaintCpuUs = paintTimer.nsecsElapsed() / 1000;
        statistics->lastUploadCpuUs = uploadMicroseconds;
        statistics->lastGpuTimeUs = implementation_->lastGpuTimeUs;
        statistics->textureBytes = textureBytes;
    }
    return true;
}

bool OpenGLGridRenderer::isInitialized() const noexcept
{
    return implementation_->initialized;
}
