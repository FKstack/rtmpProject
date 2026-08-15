#include "render/EmbeddedGlCapabilities.h"

#include <utility>

namespace {

// 生产 YUV renderer 的最低纹理需求：单路 1920x1080 需要 2048 级纹理；
// 同时绑定 Y/U/V 或 Y/UV，最多使用 3 个纹理单元。
constexpr int kMinimumMaxTextureSize = 2048;
constexpr int kMinimumCombinedTextureUnits = 3;

[[nodiscard]] bool versionMeetsRequirement(
    const EmbeddedGlCapabilities &capabilities
) noexcept
{
    if (capabilities.isOpenGles) {
        return capabilities.actualMajor >= 3;
    }
    return capabilities.actualMajor > 3 ||
           (capabilities.actualMajor == 3 && capabilities.actualMinor >= 3);
}

[[nodiscard]] EmbeddedGlQualification failed(QString reason)
{
    EmbeddedGlQualification result;
    result.qualified = false;
    result.reason = std::move(reason);
    return result;
}

} // namespace

EmbeddedGlQualification qualifyEmbeddedGlCapabilities(
    const EmbeddedGlCapabilities &capabilities
)
{
    if (capabilities.qpaPlatform.compare(
            QStringLiteral("linuxfb"), Qt::CaseInsensitive) == 0) {
        return failed(QStringLiteral(
            "QPA platform is linuxfb; there is no EGL/GLES path, use CPU."
        ));
    }
    if (!versionMeetsRequirement(capabilities)) {
        return failed(
            capabilities.isOpenGles
                ? QStringLiteral(
                      "Actual OpenGL ES context version %1.%2 is below the "
                      "required 3.0."
                  ).arg(capabilities.actualMajor).arg(capabilities.actualMinor)
                : QStringLiteral(
                      "Actual Desktop OpenGL context version %1.%2 is below "
                      "the required 3.3."
                  ).arg(capabilities.actualMajor).arg(capabilities.actualMinor)
        );
    }
    if (capabilities.maxTextureSize < kMinimumMaxTextureSize) {
        return failed(QStringLiteral(
            "GL_MAX_TEXTURE_SIZE %1 is below the required %2."
        ).arg(capabilities.maxTextureSize).arg(kMinimumMaxTextureSize));
    }
    if (capabilities.maxCombinedTextureUnits < kMinimumCombinedTextureUnits) {
        return failed(QStringLiteral(
            "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS %1 is below the required %2."
        ).arg(capabilities.maxCombinedTextureUnits)
              .arg(kMinimumCombinedTextureUnits));
    }
    if (!capabilities.supportsRequiredRedRgTextures) {
        return failed(QStringLiteral(
            "Required R8/RG8 texture allocation failed on the actual context."
        ));
    }
    if (!capabilities.supportsRequiredUnpackRowLength) {
        return failed(QStringLiteral(
            "GL_UNPACK_ROW_LENGTH is not accepted by the actual context."
        ));
    }
    if (!capabilities.shaderSmokePassed) {
        return failed(QStringLiteral(
            "Production YUV shader compile/link smoke failed."
        ));
    }
    if (!capabilities.framebufferComplete) {
        return failed(QStringLiteral(
            "Framebuffer completeness check failed on the actual context."
        ));
    }

    EmbeddedGlQualification result;
    result.qualified = true;
    return result;
}
