#include <cstdlib>

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QImage>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/VideoRenderWidget.h"

namespace {

constexpr int kSmokeTimeoutMs = 10'000;

QImage createTestFrame()
{
    QImage image(320, 180, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        auto *scanLine = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            scanLine[x] = qRgba(
                x * 255 / image.width(),
                y * 255 / image.height(),
                160,
                255
            );
        }
    }
    return image;
}

} // namespace

int main(int argc, char *argv[])
{
#if defined(Q_OS_WIN)
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(2, 0);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RtmpMonitorQtOpenGLSmoke"));

    QWidget window;
    window.setWindowTitle(QStringLiteral("RtmpMonitor Qt + OpenGL Smoke"));
    window.resize(640, 360);

    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *widget = new VideoRenderWidget(&window);
    layout->addWidget(widget);

    bool initialized = false;
    QObject::connect(
        widget,
        &VideoRenderWidget::openGLInitialized,
        &application,
        [&initialized](
            bool success,
            const QString &vendor,
            const QString &renderer,
            const QString &version
        ) {
            qInfo().noquote()
                << QStringLiteral("vendor=%1").arg(vendor)
                << QStringLiteral("renderer=%1").arg(renderer)
                << QStringLiteral("version=%1").arg(version);
            initialized =
                success &&
                !vendor.isEmpty() &&
                !renderer.isEmpty() &&
                !version.isEmpty();
        }
    );
    QObject::connect(
        widget,
        &VideoRenderWidget::frameRendered,
        &application,
        [&application, &initialized] {
            if (initialized) {
                application.exit(EXIT_SUCCESS);
            }
        }
    );

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        [&application] {
            qCritical() << "Qt + OpenGL smoke test timed out.";
            application.exit(EXIT_FAILURE);
        }
    );
    timeout.start(kSmokeTimeoutMs);

    widget->setFrame(createTestFrame());
    window.show();
    return application.exec();
}
