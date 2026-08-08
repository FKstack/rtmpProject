#include <cstdlib>
#include <cstdio>
#include <memory>
#include <type_traits>

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPalette>
#include <QPixmap>
#include <QSizePolicy>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#include "app/StyleLoader.h"
#include "core/Singleton.h"
#include "ui/FullscreenControlBar.h"
#include "ui/FullscreenVideoWindow.h"
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
        // Windows Debug 版 Qt 可能只写调试输出；stderr 确保 CTest 能捕获失败原因。
        const QByteArray utf8Message = message.toUtf8();
        std::fprintf(stderr, "%s\n", utf8Message.constData());
        std::fflush(stderr);
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
        !expect(application.styleSheet().contains(QStringLiteral("background-color: transparent")),
                QStringLiteral("内置 QSS 应让视频锚点透出共享画布。"))) {
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
    if (!expect(grid.addVideoWidget() != nullptr,
                QStringLiteral("应能从空状态创建第一个视频格。"))) {
        return EXIT_FAILURE;
    }
    grid.resize(1280, 720);
    // 触发布局计算，确保测试访问的是已完成初始布局的控件树。
    grid.show();
    application.processEvents();

    if (!expect(grid.videoWidgetCount() == 1, QStringLiteral("初始视频格子数量应为 1。")) ||
        !expect(grid.gridDimensions().rows == 1 && grid.gridDimensions().columns == 1,
                QStringLiteral("初始布局应为 1x1。")) ||
        !expect(grid.videoWidgetAt(0)->deviceName() == QStringLiteral("Camera 01"),
                QStringLiteral("初始设备名称应为 Camera 01。"))) {
        return EXIT_FAILURE;
    }

    grid.videoWidgetAt(0)->showFrame();
    application.processEvents();
    auto *initialVideoSurface =
        grid.videoWidgetAt(0)->findChild<QFrame *>(QStringLiteral("videoSurface"));
    auto *initialStatusLabel =
        grid.videoWidgetAt(0)->findChild<QLabel *>(QStringLiteral("statusLabel"));
    if (!expect(initialVideoSurface != nullptr && initialStatusLabel != nullptr,
                QStringLiteral("视频格应包含渲染表面和状态标签。"))) {
        return EXIT_FAILURE;
    }
    if (!expect(!initialStatusLabel->isVisible(),
                 QStringLiteral("显示视频帧后状态标签应隐藏。"))) {
        return EXIT_FAILURE;
    }
    grid.videoWidgetAt(0)->clearFrame();
    grid.videoWidgetAt(0)->setStatusText(QStringLiteral("未连接"));
    application.processEvents();
    if (!expect(initialStatusLabel->isVisible(),
                QStringLiteral("清除视频帧后状态标签应恢复显示。"))) {
        return EXIT_FAILURE;
    }

    for (int expectedCount = 2; expectedCount <= 4; ++expectedCount) {
        bool widgetAdded = false;
        QEventLoop addLoop;
        QTimer addTimeout;
        addTimeout.setSingleShot(true);
        const QMetaObject::Connection addedConnection =
            QObject::connect(&grid, &VideoGridWidget::videoWidgetAdded, &addLoop,
                             [&widgetAdded, &addLoop](VideoWidget *) {
                                 widgetAdded = true;
                                 addLoop.quit();
                             });
        QObject::connect(&addTimeout, &QTimer::timeout, &addLoop, &QEventLoop::quit);

        if (!expect(grid.addVideoWidget() != nullptr,
                    QStringLiteral("动态视频格应成功创建。"))) {
            return EXIT_FAILURE;
        }
        addTimeout.start(1000);
        addLoop.exec();
        addTimeout.stop();
        QObject::disconnect(addedConnection);
        if (!expect(widgetAdded && grid.videoWidgetCount() == expectedCount,
                    QStringLiteral("添加动画结束后数量应正确更新。"))) {
            return EXIT_FAILURE;
        }
    }

    if (!expect(grid.layout() != nullptr && grid.layout()->count() == 4,
                QStringLiteral("动态 2x2 网格应包含 4 个控件。"))) {
        return EXIT_FAILURE;
    }

    for (int index = 0; index < grid.videoWidgetCount(); ++index) {
        const auto *videoWidget = grid.videoWidgetAt(index);
        const QString expectedDeviceName =
            QStringLiteral("Camera %1").arg(index + 1, 2, 10, QLatin1Char('0'));

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

    auto *fullscreenSource = grid.videoWidgetAt(0);
    grid.bindVideoStream(
        fullscreenSource, 1, std::make_shared<LatestFrameMailbox>()
    );
    auto *fullscreenSurface =
        fullscreenSource->findChild<QFrame *>(QStringLiteral("videoSurface"));
    auto *sourceLayout = qobject_cast<QVBoxLayout *>(fullscreenSource->layout());
    if (!expect(fullscreenSurface != nullptr && sourceLayout != nullptr,
                QStringLiteral("全屏测试需要存在视频区域及其原始垂直布局。"))) {
        return EXIT_FAILURE;
    }

    const QString originalDeviceName = fullscreenSource->deviceName();
    const QString originalStatus = fullscreenSource->statusText();

    FullscreenVideoWindow fullscreenWindow(RendererPreference::Cpu);
    bool fullscreenExited = false;
    bool exitSignalWhileWindowVisible = false;
    bool surfaceRestoredBeforeExitSignal = false;
    int fullscreenExitSignalCount = 0;
    int reentryRequestCount = 0;
    QObject::connect(&fullscreenWindow, &FullscreenVideoWindow::fullscreenExited,
                     &fullscreenWindow,
                     [&](VideoWidget *videoWidget) {
                         fullscreenExited = videoWidget != nullptr;
                         ++fullscreenExitSignalCount;
                         exitSignalWhileWindowVisible = fullscreenWindow.isVisible();
                          surfaceRestoredBeforeExitSignal =
                              fullscreenSurface->parentWidget() == fullscreenSource &&
                              sourceLayout->indexOf(fullscreenSurface) >= 0;
                     });
    QObject::connect(fullscreenSource, &VideoWidget::fullscreenRequested,
                     &fullscreenWindow, [&reentryRequestCount](VideoWidget *) {
                         ++reentryRequestCount;
                     });

    if (!expect(fullscreenWindow.enterFullscreen(fullscreenSource),
                QStringLiteral("可见视频格应能进入全屏预览。")) ||
        !expect(!fullscreenWindow.enterFullscreen(fullscreenSource),
                QStringLiteral("已有全屏预览时必须拒绝重复进入请求。"))) {
        return EXIT_FAILURE;
    }

    application.processEvents();
    auto *controlBar = fullscreenWindow.findChild<FullscreenControlBar *>(
        QStringLiteral("fullscreenControlBar")
    );
    auto *fullscreenLayout = qobject_cast<QVBoxLayout *>(fullscreenWindow.layout());
    auto *fullscreenCanvas = fullscreenWindow.findChild<VideoCanvasHost *>(
        QStringLiteral("fullscreenVideoCanvas")
    );
    const QImage fullscreenSnapshot = fullscreenWindow.grab().toImage();
    if (!expect(fullscreenWindow.isFullscreenActive(),
                QStringLiteral("进入后全屏窗口应处于活动状态。")) ||
        !expect(fullscreenWindow.autoFillBackground() &&
                    fullscreenWindow.testAttribute(Qt::WA_OpaquePaintEvent) &&
                    fullscreenWindow.palette().color(QPalette::Window) == Qt::black,
                QStringLiteral("全屏窗口必须使用不透明的纯黑背景。")) ||
        !expect(fullscreenLayout != nullptr && fullscreenLayout->contentsMargins().isNull() &&
                    fullscreenLayout->spacing() == 0,
                QStringLiteral("全屏视频布局的 margin 和 spacing 必须为 0。")) ||
        !expect(fullscreenCanvas != nullptr &&
                    fullscreenCanvas->activeBackendName() == QStringLiteral("cpu") &&
                    fullscreenCanvas->controller()->snapshot().items.size() == 1 &&
                    fullscreenCanvas->controller()
                            ->snapshot().items.front().displayMode ==
                        VideoDisplayMode::Contain &&
                    fullscreenSurface->parentWidget() == fullscreenSource &&
                    sourceLayout->indexOf(fullscreenSurface) >= 0,
                QStringLiteral("全屏时必须创建临时画布且不得搬运原视频区域。")) ||
        !expect(!fullscreenSnapshot.isNull() &&
                    fullscreenSnapshot.pixelColor(0, 0) == QColor(Qt::black),
                QStringLiteral("全屏窗口客户区左上角必须稳定绘制为纯黑色。")) ||
        !expect(controlBar != nullptr && controlBar->isVisible() &&
                    controlBar->geometry().bottom() <= fullscreenWindow.height() - 20 &&
                    qAbs(controlBar->geometry().center().x() - fullscreenWindow.width() / 2) <= 1,
                QStringLiteral("控制栏应作为位于底部中央的可见覆盖层。"))) {
        return EXIT_FAILURE;
    }

    QMouseEvent exitDoubleClickEvent(
        QEvent::MouseButtonDblClick, QPointF(20, 20), QPointF(20, 20),
        QPointF(20, 20), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier
    );
    QApplication::sendEvent(&fullscreenWindow, &exitDoubleClickEvent);
    application.processEvents();

    if (!expect(fullscreenExited && !fullscreenWindow.isFullscreenActive(),
                QStringLiteral("退出全屏后应发送恢复完成信号。")) ||
        !expect(fullscreenExitSignalCount == 1 && exitSignalWhileWindowVisible &&
                    surfaceRestoredBeforeExitSignal && !fullscreenWindow.isVisible(),
                QStringLiteral("退出时应先恢复网格并发出一次信号，最后再隐藏黑色全屏窗口。")) ||
        !expect(reentryRequestCount == 0,
                QStringLiteral("退出全屏的同一次双击不得重新触发 VideoWidget 全屏请求。")) ||
        !expect(fullscreenSurface->parentWidget() == fullscreenSource &&
                    sourceLayout->indexOf(fullscreenSurface) >= 0,
                QStringLiteral("退出全屏后原视频区域必须始终留在网格控件中。")) ||
        !expect(grid.videoWidgetAt(0) == fullscreenSource &&
                    fullscreenSource->deviceName() == originalDeviceName &&
                    fullscreenSource->statusText() == originalStatus,
                QStringLiteral("全屏切换不得改变网格槽位、设备名称或状态文本。"))) {
        return EXIT_FAILURE;
    }

    fullscreenWindow.exitFullscreen();
    if (!expect(fullscreenExitSignalCount == 1,
                QStringLiteral("重复退出请求不得再次发出 fullscreenExited 信号。"))) {
        return EXIT_FAILURE;
    }

    bool fullscreenRequestForwarded = false;
    QObject::connect(&grid, &VideoGridWidget::fullscreenRequested, &grid,
                     [&fullscreenRequestForwarded](VideoWidget *) {
                         fullscreenRequestForwarded = true;
                     });

    if (!expect(grid.swapVideoWidgets(0, 1),
                QStringLiteral("全屏互斥测试需要成功启动交换动画。"))) {
        return EXIT_FAILURE;
    }

    QMouseEvent doubleClickEvent(
        QEvent::MouseButtonDblClick, QPointF(10, 10), QPointF(10, 10),
        QPointF(10, 10), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier
    );
    QApplication::sendEvent(grid.videoWidgetAt(0), &doubleClickEvent);
    if (!expect(!fullscreenRequestForwarded,
                QStringLiteral("交换动画期间不得转发全屏预览请求。"))) {
        return EXIT_FAILURE;
    }

    bool fullscreenMutexAnimationFinished = false;
    QEventLoop fullscreenMutexAnimationLoop;
    QTimer fullscreenMutexAnimationTimeout;
    fullscreenMutexAnimationTimeout.setSingleShot(true);
    QObject::connect(&grid, &VideoGridWidget::videoWidgetsSwapped,
                     &fullscreenMutexAnimationLoop,
                     [&fullscreenMutexAnimationFinished, &fullscreenMutexAnimationLoop](
                         int firstIndex, int secondIndex
                     ) {
                         fullscreenMutexAnimationFinished = firstIndex == 0 && secondIndex == 1;
                         fullscreenMutexAnimationLoop.quit();
                     });
    QObject::connect(&fullscreenMutexAnimationTimeout, &QTimer::timeout,
                     &fullscreenMutexAnimationLoop, &QEventLoop::quit);
    fullscreenMutexAnimationTimeout.start(1000);
    fullscreenMutexAnimationLoop.exec();
    fullscreenMutexAnimationTimeout.stop();
    if (!expect(fullscreenMutexAnimationFinished,
                QStringLiteral("全屏互斥测试中的交换动画未在预期时间内完成。"))) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
