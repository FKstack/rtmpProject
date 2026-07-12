#include <cstdlib>
#include <type_traits>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QTemporaryDir>

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

    return EXIT_SUCCESS;
}
