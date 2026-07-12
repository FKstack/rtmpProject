#include <cstdlib>

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFrame>
#include <QGridLayout>

#include "ui/VideoGridWidget.h"
#include "ui/VideoWidget.h"

namespace {

bool expect(bool condition, const QString &message)
{
    if (!condition) {
        qCritical().noquote() << message;
    }

    return condition;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    VideoGridWidget grid;
    grid.resize(1280, 720);
    grid.show();
    application.processEvents();

    if (!expect(grid.videoWidgetCount() == 4, QStringLiteral("视频格子数量应为 4。"))) {
        return EXIT_FAILURE;
    }

    if (!expect(grid.layout() != nullptr && grid.layout()->count() == 4,
                QStringLiteral("2x2 网格布局应包含 4 个控件。"))) {
        return EXIT_FAILURE;
    }

    for (int index = 0; index < grid.videoWidgetCount(); ++index) {
        const auto *videoWidget = grid.videoWidgetAt(index);
        const QString expectedDeviceName =
            QStringLiteral("camera%1").arg(index + 1, 3, 10, QLatin1Char('0'));

        if (!expect(videoWidget != nullptr, QStringLiteral("视频格子不能为空。")) ||
            !expect(videoWidget->deviceName() == expectedDeviceName,
                    QStringLiteral("设备名称与预期不一致。")) ||
            !expect(videoWidget->statusText() == QStringLiteral("未连接"),
                    QStringLiteral("初始状态应为未连接。"))) {
            return EXIT_FAILURE;
        }

        const auto *videoSurface = videoWidget->findChild<QFrame *>(QStringLiteral("videoSurface"));
        if (!expect(videoSurface != nullptr,
                    QStringLiteral("视频格子应包含黑色视频占位区域。")) ||
            !expect(videoSurface->palette().color(QPalette::Window) == QColor(Qt::black),
                    QStringLiteral("视频占位区域的背景应为黑色。"))) {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
