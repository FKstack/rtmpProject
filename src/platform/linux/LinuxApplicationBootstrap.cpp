#include "linux/LinuxApplicationBootstrap.h"

#include <QSurfaceFormat>

#include <cstring>

QString LinuxApplicationBootstrap::requestedQpaPlatform(int argc, char *argv[])
{
    // Qt 允许 QT_QPA_PLATFORM 使用冒号分隔的回退列表，取第一个有效项。
    const QByteArray environmentValue = qgetenv("QT_QPA_PLATFORM");
    if (!environmentValue.isEmpty()) {
        const int separator = environmentValue.indexOf(':');
        return QString::fromLocal8Bit(
            separator >= 0 ? environmentValue.left(separator) : environmentValue
        ).trimmed().toLower();
    }
    for (int index = 1; index < argc - 1; ++index) {
        if (std::strcmp(argv[index], "-platform") == 0) {
            return QString::fromLocal8Bit(argv[index + 1]).trimmed().toLower();
        }
    }
    return {};
}

LinuxBootstrapResult LinuxApplicationBootstrap::configureSurfaceFormat(
    bool buildHasOpenGl,
    const QString &requestedRenderer,
    int argc,
    char *argv[]
)
{
    LinuxBootstrapResult result;
    const QString qpaCandidate = requestedQpaPlatform(argc, argv);

    if (!buildHasOpenGl) {
        result.note = QStringLiteral(
            "RASTER build without an OpenGL backend; no ES surface format requested."
        );
        return result;
    }
    if (requestedRenderer == QStringLiteral("cpu")) {
        result.note = QStringLiteral(
            "--renderer=cpu requested; no ES surface format requested."
        );
        return result;
    }
    if (qpaCandidate == QStringLiteral("linuxfb")) {
        result.note = QStringLiteral(
            "QPA candidate is linuxfb; no ES surface format requested."
        );
        return result;
    }

    QSurfaceFormat surfaceFormat;
    surfaceFormat.setRenderableType(QSurfaceFormat::OpenGLES);
    surfaceFormat.setVersion(3, 0);
    surfaceFormat.setProfile(QSurfaceFormat::NoProfile);
    surfaceFormat.setDepthBufferSize(0);
    surfaceFormat.setStencilBufferSize(0);
    surfaceFormat.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(surfaceFormat);

    result.es3SurfaceFormatRequested = true;
    if (!qpaCandidate.isEmpty()) {
        result.note = QStringLiteral(
            "Requested OpenGL ES 3.0 default surface format for QPA '%1'."
        ).arg(qpaCandidate);
    }
    return result;
}

QString LinuxApplicationBootstrap::requestedRendererFromArgs(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--renderer") && index + 1 < argc) {
            return QString::fromLocal8Bit(argv[index + 1]).trimmed().toLower();
        }
        if (argument.startsWith(QStringLiteral("--renderer="))) {
            return argument.mid(QStringLiteral("--renderer=").size())
                .trimmed()
                .toLower();
        }
    }
    return QStringLiteral("auto");
}
