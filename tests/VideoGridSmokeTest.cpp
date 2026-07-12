#include <cstdlib>
#include <type_traits>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QObject>
#include <QTemporaryDir>
#include <QTimer>

#include "app/StyleLoader.h"
#include "core/Singleton.h"
#include "ui/VideoGridWidget.h"
#include "ui/VideoWidget.h"

namespace {

static_assert(!std::is_copy_constructible_v<StyleLoader>);
static_assert(!std::is_copy_assignable_v<StyleLoader>);
static_assert(!std::is_move_constructible_v<StyleLoader>);
static_assert(!std::is_move_assignable_v<StyleLoader>);
static_assert(std::is_base_of_v<Singleton<StyleLoader>, StyleLoader>);

/**
 * @brief 输出失败原因并返回断言结果。
 *
 * 不引入 Qt Test 模块，仍为失败场景保留足够的控制台诊断信息。
 *
 * @param condition 要验证的条件。
 * @param message 条件失败时输出的中文说明。
 * @return 条件是否成立。
 */
bool expect(bool condition, const QString &message)
{
    if (!condition) {
        qCritical().noquote() << message;
    }

    return condition;
}

/**
 * @brief 在指定目录中创建 UTF-8 编码的外部 QSS 文件。
 *
 * @param directory QSS 文件所在目录。
 * @param contents 要写入的 QSS 内容。
 * @return 写入成功时返回 true。
 */
bool writeStyleFile(const QString &directory, const QString &contents)
{
    QFile styleFile(QDir(directory).filePath(QStringLiteral("app.qss")));
    if (!styleFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    return styleFile.write(contents.toUtf8()) == contents.toUtf8().size();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    const auto *firstStyleLoader = &StyleLoader::instance();
    const auto *secondStyleLoader = &StyleLoader::instance();
    if (!expect(firstStyleLoader == secondStyleLoader,
                QStringLiteral("StyleLoader 应始终返回同一个单例实例。"))) {
        return EXIT_FAILURE;
    }

    QTemporaryDir missingExternalStyleDirectory;
    if (!expect(missingExternalStyleDirectory.isValid(),
                QStringLiteral("无法创建样式回退测试目录。"))) {
        return EXIT_FAILURE;
    }

    StyleLoadOptions resourceOptions;
    resourceOptions.externalStyleDirectory = missingExternalStyleDirectory.path();
    const StyleLoadResult resourceResult =
        StyleLoader::instance().applyApplicationStyle(application, resourceOptions);
    if (!expect(resourceResult.applied && resourceResult.source == StyleSource::QtResource,
                QStringLiteral("缺少外部样式时应回退到 QRC 样式。")) ||
        !expect(application.styleSheet().contains(
                    QStringLiteral("QFrame[styleRole=\"videoWidget\"]")),
                QStringLiteral("内置 QSS 应限定 videoWidget 的样式作用域。")) ||
        !expect(application.styleSheet().contains(QStringLiteral("background-color: #000000")),
                QStringLiteral("内置 QSS 应定义黑色视频区域背景。"))) {
        return EXIT_FAILURE;
    }

    QTemporaryDir externalStyleDirectory;
    const QString externalStyle = QStringLiteral("QWidget { color: rgb(1, 2, 3); }\n");
    if (!expect(externalStyleDirectory.isValid() &&
                    writeStyleFile(externalStyleDirectory.path(), externalStyle),
                QStringLiteral("无法创建外部样式测试文件。"))) {
        return EXIT_FAILURE;
    }

    StyleLoadOptions externalOptions;
    externalOptions.externalStyleDirectory = externalStyleDirectory.path();
    const StyleLoadResult externalResult =
        StyleLoader::instance().applyApplicationStyle(application, externalOptions);
    if (!expect(externalResult.applied && externalResult.source == StyleSource::ExternalFile,
                QStringLiteral("外部 QSS 应优先于内置资源加载。")) ||
        !expect(application.styleSheet() == externalStyle,
                QStringLiteral("外部 QSS 内容应完整应用到 QApplication。"))) {
        return EXIT_FAILURE;
    }

    const QString unreadableStylePath =
        QDir(missingExternalStyleDirectory.path()).filePath(QStringLiteral("app.qss"));
    if (!expect(QDir().mkpath(unreadableStylePath),
                QStringLiteral("无法创建不可读外部样式目录。"))) {
        return EXIT_FAILURE;
    }

    const StyleLoadResult unreadableResult =
        StyleLoader::instance().applyApplicationStyle(application, resourceOptions);
    if (!expect(unreadableResult.applied && unreadableResult.source == StyleSource::QtResource,
                QStringLiteral("不可读外部 QSS 时应回退到 QRC 样式。"))) {
        return EXIT_FAILURE;
    }

    VideoGridWidget grid;
    grid.resize(1280, 720);
    // 触发布局计算，确保测试访问的是已完成初始布局的控件树。
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
            !expect(videoWidget->property("styleRole") == QStringLiteral("videoWidget"),
                    QStringLiteral("视频格子必须声明供 QSS 使用的 styleRole。"))) {
            return EXIT_FAILURE;
        }
    }

    auto *firstWidget = grid.videoWidgetAt(0);
    auto *lastWidget = grid.videoWidgetAt(3);
    auto *firstSurface = firstWidget->findChild<QFrame *>(QStringLiteral("videoSurface"));
    auto *lastSurface = lastWidget->findChild<QFrame *>(QStringLiteral("videoSurface"));
    firstWidget->setDeviceName(QStringLiteral("source-device"));
    firstWidget->setStatusText(QStringLiteral("source-status"));
    lastWidget->setDeviceName(QStringLiteral("target-device"));
    lastWidget->setStatusText(QStringLiteral("target-status"));

    if (!expect(!grid.swapVideoWidgets(-1, 0) &&
                    !grid.swapVideoWidgets(0, grid.videoWidgetCount()) &&
                    !grid.swapVideoWidgets(0, 0),
                QStringLiteral("越界或相同索引的交换请求必须被拒绝。"))) {
        return EXIT_FAILURE;
    }

    bool animationFinished = false;
    QEventLoop animationLoop;
    QTimer animationTimeout;
    animationTimeout.setSingleShot(true);
    QObject::connect(&grid, &VideoGridWidget::videoWidgetsSwapped, &animationLoop,
                     [&animationFinished, &animationLoop](int firstIndex, int secondIndex) {
                         animationFinished = firstIndex == 0 && secondIndex == 3;
                         animationLoop.quit();
                     });
    QObject::connect(&animationTimeout, &QTimer::timeout, &animationLoop, &QEventLoop::quit);

    if (!expect(grid.swapVideoWidgets(0, 3),
                QStringLiteral("有效槽位交换应成功启动动画。")) ||
        !expect(!grid.swapVideoWidgets(0, 1),
                QStringLiteral("动画进行中必须拒绝新的交换请求。"))) {
        return EXIT_FAILURE;
    }

    animationTimeout.start(1000);
    animationLoop.exec();
    animationTimeout.stop();

    if (!expect(animationFinished, QStringLiteral("交换动画未在预期时间内完成。")) ||
        !expect(grid.videoWidgetAt(0) == lastWidget && grid.videoWidgetAt(3) == firstWidget,
                QStringLiteral("交换后两个槽位必须指向对方原有的 VideoWidget 对象。")) ||
        !expect(grid.videoWidgetAt(0)->deviceName() == QStringLiteral("target-device") &&
                    grid.videoWidgetAt(0)->statusText() == QStringLiteral("target-status") &&
                    grid.videoWidgetAt(3)->deviceName() == QStringLiteral("source-device") &&
                    grid.videoWidgetAt(3)->statusText() == QStringLiteral("source-status"),
                QStringLiteral("标题和状态文本必须随实际 VideoWidget 对象移动。")) ||
        !expect(grid.videoWidgetAt(0)->findChild<QFrame *>(QStringLiteral("videoSurface")) ==
                    lastSurface &&
                    grid.videoWidgetAt(3)->findChild<QFrame *>(QStringLiteral("videoSurface")) ==
                        firstSurface,
                QStringLiteral("视频区域子控件必须随实际 VideoWidget 对象移动。")) ||
        !expect(firstWidget->isVisible() && lastWidget->isVisible() &&
                    firstWidget->isDragEnabled() && lastWidget->isDragEnabled(),
                QStringLiteral("动画结束后视频格应恢复可见且允许再次拖拽。")) ||
        !expect(grid.layout()->count() == 4,
                QStringLiteral("交换后网格布局仍应包含四个视频格。"))) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
