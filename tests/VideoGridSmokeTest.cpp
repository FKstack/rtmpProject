#include <cstdlib>
#include <cstdio>
#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QImage>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPalette>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <QWindow>
#include <qt_windows.h>
#endif

#include "app/StyleLoader.h"
#include "core/Singleton.h"
#include "media/LatestFrameMailbox.h"
#include "media/VideoFrame.h"
#include "ui/FullscreenControlBar.h"
#include "ui/FullscreenVideoWindow.h"
#include "ui/VideoGridWidget.h"
#include "ui/VideoWidget.h"

namespace {

void sendMouseMove(QWidget *target, const QPoint &position)
{
    const QPoint globalPosition = target->mapToGlobal(position);
    QCursor::setPos(globalPosition);
    QMouseEvent event(
        QEvent::MouseMove,
        QPointF(position),
        QPointF(globalPosition),
        Qt::NoButton,
        Qt::NoButton,
        Qt::NoModifier
    );
    QApplication::sendEvent(target, &event);
}

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

void waitForEvents(int milliseconds)
{
    QEventLoop eventLoop;
    QTimer::singleShot(milliseconds, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
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

VideoFrame makeScreenshotTestFrame(std::uint64_t sequence)
{
    constexpr int width = 64;
    constexpr int height = 48;
    constexpr int chromaWidth = width / 2;
    constexpr int chromaHeight = height / 2;
    std::vector<std::uint8_t> y(width * height, 82);
    std::vector<std::uint8_t> u(chromaWidth * chromaHeight, 90);
    std::vector<std::uint8_t> v(chromaWidth * chromaHeight, 240);
    std::array<VideoPlaneView, VideoFrame::kMaximumPlanes> planes {{
        {y.data(), width, width, height},
        {u.data(), chromaWidth, chromaWidth, chromaHeight},
        {v.data(), chromaWidth, chromaWidth, chromaHeight},
    }};
    const auto frame = VideoFrame::copyFromPlanes(
        width, height, VideoPixelFormat::Yuv420P8, planes,
        0, 1, {1, 30},
        {
            VideoColorPrimaries::Bt709,
            VideoTransferFunction::Bt709,
            VideoMatrixCoefficients::Bt709,
            VideoColorRange::Limited,
        },
        sequence, 1, 0
    );
    Q_ASSERT(frame.has_value());
    return *frame;
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
        !expect(application.property("rtmpMonitorBaseStyle").toString() ==
                    QStringLiteral("Fusion"),
                QStringLiteral("统一主题应使用 Fusion 基础样式。")) ||
        !expect(application.palette().color(QPalette::Window) ==
                    QColor(QStringLiteral("#0B1118")),
                QStringLiteral("统一主题应应用深石墨窗口 Palette。")) ||
        !expect(application.styleSheet().contains(
                    QStringLiteral("QFrame[styleRole=\"videoWidget\"]")),
                QStringLiteral("内置 QSS 应限定 videoWidget 的样式作用域。")) ||
        !expect(application.styleSheet().contains(
                    QStringLiteral("QMainWindow#mainWindow")),
                QStringLiteral("内置 QSS 应包含主窗口作用域。")) ||
        !expect(application.styleSheet().contains(QStringLiteral("background-color: transparent")),
                QStringLiteral("内置 QSS 应让视频锚点透出共享画布。")) ||
        !expect(!QIcon(QStringLiteral(":/icons/rtmp-monitor-128.png")).isNull(),
                QStringLiteral("应用图标应编入 QRC。"))) {
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

    StyleLoadOptions invalidOptions;
    invalidOptions.styleFileName = QStringLiteral("../invalid.qss");
    const QString styleBeforeInvalidRequest = application.styleSheet();
    const QColor paletteBeforeInvalidRequest =
        application.palette().color(QPalette::Window);
    const StyleLoadResult invalidResult =
        StyleLoader::instance().applyApplicationStyle(application, invalidOptions);
    if (!expect(!invalidResult.applied,
                QStringLiteral("非法样式文件名应被拒绝。")) ||
        !expect(application.styleSheet() == styleBeforeInvalidRequest,
                QStringLiteral("样式加载失败不得污染既有 QSS。")) ||
        !expect(application.palette().color(QPalette::Window) ==
                    paletteBeforeInvalidRequest,
                QStringLiteral("样式加载失败不得污染既有 Palette。"))) {
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

    auto *audioButton = grid.videoWidgetAt(0)->findChild<QToolButton *>(
        QStringLiteral("audioToggleButton")
    );
    int audioToggleCount = 0;
    QObject::connect(
        grid.videoWidgetAt(0), &VideoWidget::audioToggleRequested,
        &grid, [&audioToggleCount](VideoWidget *) { ++audioToggleCount; }
    );
    if (!expect(
            audioButton != nullptr && !audioButton->isEnabled() &&
                grid.videoWidgetAt(0)->audioPlaybackState() ==
                    AudioPlaybackState::Unavailable,
            QStringLiteral("视频格默认必须显示不可用且不可误触的音频状态。")
        )) {
        return EXIT_FAILURE;
    }
    grid.videoWidgetAt(0)->setAudioPlaybackState(
        AudioPlaybackState::Muted, false
    );
    audioButton->click();
    if (!expect(
            audioButton->isEnabled() && audioToggleCount == 1 &&
                audioButton->text() == QStringLiteral("开启声音"),
            QStringLiteral("发现 AAC 后音频按钮应可用并发出唯一切换请求。")
        )) {
        return EXIT_FAILURE;
    }
    grid.videoWidgetAt(0)->setAudioPlaybackState(
        AudioPlaybackState::Unavailable, false
    );

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
    auto fullscreenMailbox = std::make_shared<LatestFrameMailbox>();
    grid.bindVideoStream(
        fullscreenSource, 1, fullscreenMailbox
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
    fullscreenSource->setAudioPlaybackState(
        AudioPlaybackState::Muted, false
    );

    // Reproduce the Windows regression from a fresh fullscreen window: when a
    // stream already had a visible frame, the hidden canvas used to publish a
    // stale, small viewport before the fullscreen layout assigned its size.
    if (!expect(fullscreenMailbox->submit(makeScreenshotTestFrame(1)),
                QStringLiteral("无法提交首次全屏几何回归帧。"))) {
        return EXIT_FAILURE;
    }
    fullscreenSource->showFrame();
    application.processEvents();

    FullscreenVideoWindow firstFrameFullscreenWindow(RendererPreference::Cpu);
    QObject::connect(
        &firstFrameFullscreenWindow,
        &FullscreenVideoWindow::fullscreenRestoreRequested,
        &firstFrameFullscreenWindow,
        [&firstFrameFullscreenWindow](VideoWidget *) {
            QTimer::singleShot(
                0, &firstFrameFullscreenWindow,
                &FullscreenVideoWindow::completeExitTransition
            );
        }
    );
    firstFrameFullscreenWindow.setCursor(Qt::BlankCursor);
    if (!expect(firstFrameFullscreenWindow.enterFullscreen(fullscreenSource),
                QStringLiteral("已有帧的首次全屏进入必须成功。"))) {
        return EXIT_FAILURE;
    }
    application.processEvents();
    application.processEvents();
    auto *firstFrameFullscreenCanvas =
        firstFrameFullscreenWindow.findChild<VideoCanvasHost *>(
            QStringLiteral("fullscreenVideoCanvas")
        );
    const RenderSnapshot firstEntrySnapshot =
        firstFrameFullscreenCanvas != nullptr
            ? firstFrameFullscreenCanvas->controller()->snapshot()
            : RenderSnapshot {};
    const QSize expectedFramebufferSize = firstFrameFullscreenCanvas != nullptr
        ? QSize(
              qRound(firstFrameFullscreenCanvas->width() *
                     firstFrameFullscreenCanvas->devicePixelRatioF()),
              qRound(firstFrameFullscreenCanvas->height() *
                     firstFrameFullscreenCanvas->devicePixelRatioF())
          )
        : QSize();
    const QImage firstEntryFramebuffer = firstFrameFullscreenCanvas != nullptr
        ? firstFrameFullscreenCanvas->grabFramebufferImage()
        : QImage();
    if (!expect(firstFrameFullscreenCanvas != nullptr &&
                    firstFrameFullscreenCanvas->isVisible() &&
                    firstFrameFullscreenCanvas->rect() ==
                        firstFrameFullscreenWindow.rect(),
                QStringLiteral("首次全屏画布必须立即占满窗口客户区。")) ||
        !expect(firstEntrySnapshot.logicalCanvasSize ==
                    firstFrameFullscreenCanvas->size() &&
                    firstEntrySnapshot.items.size() == 1 &&
                    firstEntrySnapshot.items.front().tileRect ==
                        firstFrameFullscreenCanvas->rect() &&
                    firstEntrySnapshot.items.front().videoViewport ==
                        firstFrameFullscreenCanvas->rect(),
                QStringLiteral("首次全屏 Snapshot 必须使用最终画布尺寸。")) ||
        !expect(!firstEntryFramebuffer.isNull() &&
                    firstEntryFramebuffer.size() == expectedFramebufferSize,
                QStringLiteral("首次全屏 framebuffer 必须匹配画布及 DPR。")) ||
        !expect(firstFrameFullscreenWindow.cursor().shape() == Qt::ArrowCursor,
                QStringLiteral("首次进入必须同步恢复箭头光标。"))) {
        return EXIT_FAILURE;
    }

    firstFrameFullscreenWindow.exitFullscreen();
    application.processEvents();
    application.processEvents();
    firstFrameFullscreenWindow.setCursor(Qt::BlankCursor);
    if (!expect(firstFrameFullscreenWindow.enterFullscreen(fullscreenSource),
                QStringLiteral("第二次全屏进入必须成功。")) ||
        !expect(firstFrameFullscreenWindow.cursor().shape() == Qt::ArrowCursor,
                QStringLiteral("第二次进入必须立即清除旧 BlankCursor。"))) {
        return EXIT_FAILURE;
    }
    application.processEvents();
    if (!expect(firstFrameFullscreenWindow.cursor().shape() == Qt::ArrowCursor,
                QStringLiteral("旧会话计时回调不得在第二次进入后隐藏光标。"))) {
        return EXIT_FAILURE;
    }
    firstFrameFullscreenWindow.exitFullscreen();
    application.processEvents();
    application.processEvents();
    fullscreenSource->clearFrame();
    fullscreenMailbox = std::make_shared<LatestFrameMailbox>();
    grid.bindVideoStream(fullscreenSource, 1, fullscreenMailbox);
    application.processEvents();

    FullscreenVideoWindow fullscreenWindow(RendererPreference::Cpu);
    bool fullscreenExited = false;
    bool exitSignalWhileWindowVisible = false;
    bool surfaceRestoredBeforeExitSignal = false;
    int fullscreenExitSignalCount = 0;
    int reentryRequestCount = 0;
    bool transitionOverlayObserved = false;
    bool transitionCanvasHidden = false;
    int restoreRequestCount = 0;
    int fullscreenMuteRequestCount = 0;
    QObject::connect(
        &fullscreenWindow, &FullscreenVideoWindow::muteRequested,
        &fullscreenWindow, [&fullscreenMuteRequestCount](VideoWidget *) {
            ++fullscreenMuteRequestCount;
        }
    );
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
    const QMetaObject::Connection automaticRestoreConnection = QObject::connect(
        &fullscreenWindow,
        &FullscreenVideoWindow::fullscreenRestoreRequested,
        &fullscreenWindow,
        [&](VideoWidget *) {
            ++restoreRequestCount;
            auto *overlay = fullscreenWindow.findChild<QLabel *>(
                QStringLiteral("fullscreenExitTransitionOverlay")
            );
            auto *canvas = fullscreenWindow.findChild<VideoCanvasHost *>(
                QStringLiteral("fullscreenVideoCanvas")
            );
            transitionOverlayObserved =
                overlay != nullptr && overlay->isVisible() &&
                fullscreenWindow.isVisible();
            transitionCanvasHidden = canvas != nullptr && !canvas->isVisible();
            QTimer::singleShot(
                0, &fullscreenWindow,
                &FullscreenVideoWindow::completeExitTransition
            );
        }
    );

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
    auto *fullscreenDeviceNameLabel = fullscreenWindow.findChild<QLabel *>(
        QStringLiteral("fullscreenDeviceNameLabel")
    );
    auto *fullscreenStreamInfoLabel = fullscreenWindow.findChild<QLabel *>(
        QStringLiteral("fullscreenStreamInfoLabel")
    );
    auto *fullscreenLayout = qobject_cast<QVBoxLayout *>(fullscreenWindow.layout());
    auto *fullscreenCanvas = fullscreenWindow.findChild<VideoCanvasHost *>(
        QStringLiteral("fullscreenVideoCanvas")
    );
    auto *revealZone = fullscreenWindow.findChild<QWidget *>(
        QStringLiteral("fullscreenControlRevealZone")
    );
    auto *screenshotButton = fullscreenWindow.findChild<QPushButton *>(
        QStringLiteral("screenshotButton")
    );
    auto *screenshotShortcut = fullscreenWindow.findChild<QShortcut *>();
    auto *screenshotToast = fullscreenWindow.findChild<QFrame *>(
        QStringLiteral("screenshotToast")
    );
    auto *screenshotThumbnail = fullscreenWindow.findChild<QLabel *>(
        QStringLiteral("screenshotThumbnailLabel")
    );
    auto *screenshotMessage = fullscreenWindow.findChild<QLabel *>(
        QStringLiteral("screenshotMessageLabel")
    );
    auto *controlBarAnimation = fullscreenWindow.findChild<QPropertyAnimation *>(
        QStringLiteral("fullscreenControlBarAnimation")
    );
    auto *cursorHideTimer = fullscreenWindow.findChild<QTimer *>(
        QStringLiteral("fullscreenCursorHideTimer")
    );
    const QImage fullscreenSnapshot = fullscreenWindow.grab().toImage();
    QKeyEvent muteKey(
        QEvent::KeyPress, Qt::Key_M, Qt::NoModifier
    );
    QApplication::sendEvent(&fullscreenWindow, &muteKey);
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
                    fullscreenCanvas->isHidden() &&
                    fullscreenCanvas->activeBackendName() == QStringLiteral("cpu") &&
                    fullscreenCanvas->testAttribute(
                        Qt::WA_TransparentForMouseEvents
                    ) &&
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
        !expect(fullscreenMuteRequestCount == 1,
                QStringLiteral("全屏 M 快捷键应切换当前稳定流的音频状态。")) ||
        !expect(controlBar != nullptr && revealZone != nullptr &&
                    controlBar->parentWidget() == revealZone &&
                    controlBar->isVisible() &&
                    controlBar->mapTo(&fullscreenWindow, QPoint(0, 0)).y() >=
                        fullscreenWindow.height() - revealZone->height() &&
                    controlBar->mapTo(&fullscreenWindow, controlBar->rect().bottomRight()).y() <=
                        fullscreenWindow.height() - 20 &&
                    qAbs(controlBar->mapTo(&fullscreenWindow,
                                           controlBar->rect().center()).x() -
                         fullscreenWindow.width() / 2) <= 1,
                QStringLiteral("控制栏应作为位于底部中央的可见覆盖层。")) ||
        !expect(revealZone != nullptr && revealZone->height() == 96 &&
                    controlBarAnimation != nullptr && cursorHideTimer != nullptr &&
                    screenshotButton != nullptr && screenshotShortcut != nullptr &&
                    screenshotShortcut->key() ==
                        QKeySequence(QStringLiteral("Ctrl+Shift+S")) &&
                    screenshotButton->toolTip().contains(
                        QStringLiteral("Ctrl+Shift+S")
                    ),
                QStringLiteral("全屏底部热区和截图快捷键必须正确创建。"))) {
        return EXIT_FAILURE;
    }

#if defined(Q_OS_WIN)
    const HWND fullscreenHwnd = reinterpret_cast<HWND>(
        fullscreenWindow.windowHandle()->winId()
    );
    if (!expect((GetWindowLongPtrW(fullscreenHwnd, GWL_STYLE) & WS_BORDER) != 0,
                QStringLiteral("Windows 全屏 OpenGL 顶层窗口必须保留 WS_BORDER。"))) {
        return EXIT_FAILURE;
    }
#endif

    waitForEvents(2800);
    if (!expect(controlBar->isVisible(),
                QStringLiteral("无视频帧时等待超过 2.5 秒后控制栏仍应可见。"))) {
        return EXIT_FAILURE;
    }

    QTemporaryDir screenshotDirectory;
    if (!expect(screenshotDirectory.isValid(),
                QStringLiteral("无法创建截图测试目录。"))) {
        return EXIT_FAILURE;
    }
    fullscreenWindow.setScreenshotOutputDirectory(screenshotDirectory.path());
    int screenshotRequestCount = 0;
    int screenshotSavedCount = 0;
    int screenshotFailedCount = 0;
    QStringList savedScreenshotPaths;
    QObject::connect(&fullscreenWindow,
                     &FullscreenVideoWindow::screenshotRequested,
                     &fullscreenWindow, [&](VideoWidget *) {
                         ++screenshotRequestCount;
                     });
    QObject::connect(&fullscreenWindow,
                     &FullscreenVideoWindow::screenshotSaved,
                     &fullscreenWindow, [&](const QString &path) {
                         ++screenshotSavedCount;
                         savedScreenshotPaths.push_back(path);
                     });
    QObject::connect(&fullscreenWindow,
                     &FullscreenVideoWindow::screenshotFailed,
                     &fullscreenWindow, [&](const QString &) {
                         ++screenshotFailedCount;
                     });

    screenshotButton->click();
    application.processEvents();
    if (!expect(screenshotRequestCount == 1 && screenshotFailedCount == 1 &&
                    QDir(screenshotDirectory.path())
                        .entryList({QStringLiteral("*.png")}, QDir::Files)
                        .isEmpty(),
                QStringLiteral("无帧截图只能提示失败，不得创建黑色 PNG。")) ||
        !expect(screenshotToast != nullptr && screenshotToast->isVisible() &&
                    screenshotMessage != nullptr &&
                    screenshotMessage->text().contains(
                        QStringLiteral("Ctrl+Shift+S")
                    ),
                QStringLiteral("无帧截图提示必须显示快捷键说明。"))) {
        return EXIT_FAILURE;
    }

    const QString updatedDeviceName = QStringLiteral("Camera 02 Current");
    const QString reconnectingStatus = QStringLiteral("连接中断，正在重连...");
    fullscreenSource->setDeviceName(updatedDeviceName);
    fullscreenSource->setStatusText(reconnectingStatus);
    application.processEvents();
    const RenderSnapshot updatedSnapshot = fullscreenCanvas->controller()->snapshot();
    if (!expect(fullscreenDeviceNameLabel != nullptr &&
                    fullscreenDeviceNameLabel->text() == updatedDeviceName &&
                    fullscreenStreamInfoLabel != nullptr &&
                    fullscreenStreamInfoLabel->text() == reconnectingStatus,
                QStringLiteral("全屏控制栏必须立即同步设备名和重连状态。")) ||
        !expect(updatedSnapshot.items.size() == 1 &&
                    updatedSnapshot.items.front().title == updatedDeviceName &&
                    updatedSnapshot.items.front().status == reconnectingStatus,
                QStringLiteral("全屏 Snapshot 必须立即同步当前摄像头状态。"))) {
        return EXIT_FAILURE;
    }

    QCursor::setPos(fullscreenWindow.mapToGlobal(QPoint(20, 20)));
    if (!expect(fullscreenMailbox->submit(makeScreenshotTestFrame(1)),
                QStringLiteral("无法提交截图测试帧。"))) {
        return EXIT_FAILURE;
    }
    fullscreenSource->showFrame();
    waitForEvents(1600);
    if (!expect(fullscreenCanvas->isVisible(),
                QStringLiteral("The fullscreen canvas must be shown when the first frame arrives.")) ||
        !expect(!controlBar->isVisible(),
                QStringLiteral("第一帧到达 1.2 秒后控制栏应滑出并隐藏。"))) {
        return EXIT_FAILURE;
    }

    sendMouseMove(
        revealZone,
        QPoint(revealZone->width() / 2, revealZone->height() - 4)
    );
    waitForEvents(220);
    if (!expect(controlBar->isVisible(),
                QStringLiteral("鼠标进入底部 96px 热区后控制栏必须滑入。"))) {
        return EXIT_FAILURE;
    }

    const QImage expectedFramebuffer = fullscreenCanvas->grabFramebufferImage();
    screenshotButton->click();
    application.processEvents();
    if (!expect(screenshotRequestCount == 2 && screenshotToast->isVisible() &&
                    screenshotThumbnail != nullptr &&
                    !screenshotThumbnail->pixmap().isNull() &&
                    screenshotMessage->text().contains(
                        QStringLiteral("Ctrl+Shift+S")
                    ),
                QStringLiteral("截图按钮必须立即显示当前画面缩略图和快捷键。"))) {
        return EXIT_FAILURE;
    }
    for (int attempt = 0; attempt < 100 && screenshotSavedCount < 1; ++attempt) {
        waitForEvents(20);
    }
    if (!expect(screenshotSavedCount == 1 && savedScreenshotPaths.size() == 1 &&
                    QFileInfo::exists(savedScreenshotPaths.front()),
                QStringLiteral("截图按钮必须在后台原子保存 PNG。"))) {
        return EXIT_FAILURE;
    }

    sendMouseMove(
        fullscreenCanvas,
        QPoint(fullscreenCanvas->width() / 2, fullscreenCanvas->height() / 3)
    );
    waitForEvents(500);
    if (!expect(!controlBar->isVisible(),
                QStringLiteral("截图后鼠标移到上方，控制栏必须按防抖时序收起。"))) {
        return EXIT_FAILURE;
    }

    // 光标允许在播放静止时隐藏，但任何落到子画布的鼠标移动都必须立即恢复。
    waitForEvents(2200);
    if (!expect(fullscreenWindow.cursor().shape() == Qt::BlankCursor,
                QStringLiteral("播放且控制栏隐藏时光标应在空闲后隐藏。"))) {
        return EXIT_FAILURE;
    }
    sendMouseMove(
        fullscreenCanvas,
        QPoint(fullscreenCanvas->width() / 2 + 20,
               fullscreenCanvas->height() / 3)
    );
    application.processEvents();
    if (!expect(fullscreenWindow.cursor().shape() != Qt::BlankCursor,
                QStringLiteral("子画布上的任意鼠标移动必须立即恢复光标。"))) {
        return EXIT_FAILURE;
    }

    // 同一方向的连续 MouseMove 不得反复把滑入动画重置到 0。
    sendMouseMove(
        revealZone,
        QPoint(revealZone->width() / 2, revealZone->height() - 4)
    );
    waitForEvents(50);
    const int animationTimeBeforeRepeatedMove = controlBarAnimation->currentTime();
    sendMouseMove(
        revealZone,
        QPoint(revealZone->width() / 2 + 10, revealZone->height() - 4)
    );
    application.processEvents();
    if (!expect(controlBarAnimation->currentTime() >= animationTimeBeforeRepeatedMove,
                QStringLiteral("底部连续 MouseMove 不得重启动画造成卡顿。"))) {
        return EXIT_FAILURE;
    }
    waitForEvents(180);
    const QImage savedFramebuffer(savedScreenshotPaths.front());
    if (!expect(!expectedFramebuffer.isNull() && !savedFramebuffer.isNull() &&
                    expectedFramebuffer.size() == savedFramebuffer.size() &&
                    expectedFramebuffer.pixelColor(
                        expectedFramebuffer.width() / 2,
                        expectedFramebuffer.height() / 2
                    ) == savedFramebuffer.pixelColor(
                        savedFramebuffer.width() / 2,
                        savedFramebuffer.height() / 2
                    ),
                QStringLiteral("保存 PNG 的像素必须来自点击瞬间的画布 framebuffer。"))) {
        return EXIT_FAILURE;
    }

    screenshotShortcut->activated();
    for (int attempt = 0; attempt < 100 && screenshotSavedCount < 2; ++attempt) {
        waitForEvents(20);
    }
    if (!expect(screenshotRequestCount == 3 && screenshotSavedCount == 2 &&
                    savedScreenshotPaths.at(0) != savedScreenshotPaths.at(1),
                QStringLiteral("Ctrl+Shift+S 必须独立触发并生成唯一文件名。"))) {
        return EXIT_FAILURE;
    }

    const QString blockedDirectoryPath = QDir(screenshotDirectory.path())
        .filePath(QStringLiteral("not-a-directory"));
    QFile blockedDirectory(blockedDirectoryPath);
    if (!expect(blockedDirectory.open(QIODevice::WriteOnly) &&
                    blockedDirectory.write("x") == 1,
                QStringLiteral("无法创建截图失败路径。"))) {
        return EXIT_FAILURE;
    }
    blockedDirectory.close();
    fullscreenWindow.setScreenshotOutputDirectory(blockedDirectoryPath);
    screenshotShortcut->activated();
    application.processEvents();
    if (!expect(screenshotRequestCount == 4 && screenshotFailedCount == 2 &&
                    screenshotMessage->text().contains(QStringLiteral("无法创建")),
                QStringLiteral("截图目录不可写时必须报告可操作错误。"))) {
        return EXIT_FAILURE;
    }
    fullscreenWindow.setScreenshotOutputDirectory(screenshotDirectory.path());

    sendMouseMove(
        fullscreenCanvas,
        QPoint(fullscreenCanvas->width() / 2, fullscreenCanvas->height() / 3)
    );
    waitForEvents(500);
    if (!expect(!controlBar->isVisible(),
                QStringLiteral("离开热区和控制栏后应在 250ms 防抖后滑出。"))) {
        return EXIT_FAILURE;
    }

    sendMouseMove(
        revealZone,
        QPoint(revealZone->width() / 2, revealZone->height() - 4)
    );
    waitForEvents(60);
    sendMouseMove(
        fullscreenCanvas,
        QPoint(fullscreenCanvas->width() / 2, fullscreenCanvas->height() / 3)
    );
    waitForEvents(60);

    fullscreenSource->setStatusText(QStringLiteral("连接中断，正在重连"));
    fullscreenSource->clearFrame();
    application.processEvents();
    if (!expect(fullscreenCanvas->isHidden(),
                QStringLiteral("The fullscreen canvas must be hidden while no frame is available.")) ||
        !expect(controlBar->isVisible() &&
                    fullscreenStreamInfoLabel->text() ==
                        QStringLiteral("连接中断，正在重连"),
                QStringLiteral("清除视频帧后控制栏和最新重连状态必须重新显示。"))) {
        return EXIT_FAILURE;
    }

    fullscreenSource->setDeviceName(originalDeviceName);
    fullscreenSource->setStatusText(originalStatus);
    application.processEvents();

    QMouseEvent exitDoubleClickEvent(
        QEvent::MouseButtonDblClick, QPointF(20, 20), QPointF(20, 20),
        QPointF(20, 20), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier
    );
    QApplication::sendEvent(&fullscreenWindow, &exitDoubleClickEvent);
    application.processEvents();

    if (!expect(fullscreenExited && !fullscreenWindow.isFullscreenActive(),
                QStringLiteral("退出全屏后应发送恢复完成信号。")) ||
        !expect(fullscreenExitSignalCount == 1 && !exitSignalWhileWindowVisible &&
                    surfaceRestoredBeforeExitSignal && !fullscreenWindow.isVisible(),
                QStringLiteral("退出时应先隐藏全屏顶层窗口，再发出一次恢复信号。")) ||
        !expect(restoreRequestCount == 1 && transitionOverlayObserved &&
                    transitionCanvasHidden,
                QStringLiteral("退出时必须先显示 raster 过渡层并隐藏全屏画布。")) ||
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

    for (int slot = 1; slot <= 2; ++slot) {
        VideoWidget *nextSource = grid.videoWidgetAt(slot);
        const StreamId nextStreamId = static_cast<StreamId>(slot + 1);
        const QString nextDeviceName =
            QStringLiteral("Camera %1").arg(slot + 1, 2, 10, QLatin1Char('0'));
        const QString nextStatus = QStringLiteral("正在连接 camera%1").arg(slot + 1);
        grid.bindVideoStream(
            nextSource, nextStreamId, std::make_shared<LatestFrameMailbox>()
        );
        nextSource->setDeviceName(nextDeviceName);
        nextSource->setStatusText(nextStatus);

        if (!expect(fullscreenWindow.enterFullscreen(nextSource),
                    QStringLiteral("连续摄像头切换应能进入全屏。"))) {
            return EXIT_FAILURE;
        }
        application.processEvents();
        const RenderSnapshot nextSnapshot = fullscreenCanvas->controller()->snapshot();
        if (!expect(fullscreenDeviceNameLabel->text() == nextDeviceName &&
                        fullscreenStreamInfoLabel->text() == nextStatus,
                    QStringLiteral("控制栏只能显示当前摄像头内容。")) ||
            !expect(nextSnapshot.items.size() == 1 &&
                        nextSnapshot.items.front().streamId == nextStreamId &&
                        nextSnapshot.items.front().title == nextDeviceName &&
                        nextSnapshot.items.front().status == nextStatus,
                    QStringLiteral("全屏 Snapshot 只能保留当前摄像头 RenderItem。"))) {
            return EXIT_FAILURE;
        }

        fullscreenWindow.exitFullscreen();
        application.processEvents();
        if (!expect(!fullscreenWindow.isVisible() &&
                        !fullscreenWindow.isFullscreenActive() &&
                        fullscreenCanvas->controller()->snapshot().items.empty() &&
                        fullscreenDeviceNameLabel->text().isEmpty() &&
                        fullscreenStreamInfoLabel->text().isEmpty(),
                    QStringLiteral("退出后不得遗留上一摄像头的 Snapshot 或控制栏文本。"))) {
            return EXIT_FAILURE;
        }
    }

    if (!expect(fullscreenExitSignalCount == 3,
                QStringLiteral("三次有效全屏退出必须各发出一次且仅一次信号。"))) {
        return EXIT_FAILURE;
    }

    QObject::disconnect(automaticRestoreConnection);
    const QMetaObject::Connection restoreRequestObserver = QObject::connect(
        &fullscreenWindow,
        &FullscreenVideoWindow::fullscreenRestoreRequested,
        &fullscreenWindow,
        [&restoreRequestCount](VideoWidget *) {
            ++restoreRequestCount;
        }
    );
    if (!expect(fullscreenWindow.enterFullscreen(fullscreenSource),
                QStringLiteral("安全超时测试应能再次进入全屏。"))) {
        return EXIT_FAILURE;
    }
    fullscreenWindow.exitFullscreen();
    if (!expect(fullscreenWindow.isFullscreenActive() &&
                    fullscreenWindow.isVisible(),
                QStringLiteral("主画布未确认前 raster 过渡窗口必须继续可见。"))) {
        return EXIT_FAILURE;
    }
    waitForEvents(900);
    if (!expect(!fullscreenWindow.isFullscreenActive() &&
                    !fullscreenWindow.isVisible() &&
                    fullscreenExitSignalCount == 4 && restoreRequestCount == 4,
                QStringLiteral("750ms 安全超时必须完成退出且只发出一次完成信号。"))) {
        return EXIT_FAILURE;
    }

    const QMetaObject::Connection repeatedRestoreConnection = QObject::connect(
        &fullscreenWindow,
        &FullscreenVideoWindow::fullscreenRestoreRequested,
        &fullscreenWindow,
        [&fullscreenWindow](VideoWidget *) {
            fullscreenWindow.completeExitTransition();
        }
    );
    for (int iteration = 0; iteration < 30; ++iteration) {
        if (!expect(fullscreenWindow.enterFullscreen(fullscreenSource),
                    QStringLiteral("第 %1 次连续全屏进入失败。")
                        .arg(iteration + 1))) {
            return EXIT_FAILURE;
        }
        fullscreenWindow.exitFullscreen();
        application.processEvents();
        if (!expect(!fullscreenWindow.isFullscreenActive() &&
                        !fullscreenWindow.isVisible() &&
                        fullscreenCanvas->controller()->snapshot().items.empty(),
                    QStringLiteral("第 %1 次连续全屏退出遗留了状态。")
                        .arg(iteration + 1))) {
            return EXIT_FAILURE;
        }
    }
    QObject::disconnect(repeatedRestoreConnection);
    QObject::disconnect(restoreRequestObserver);
    if (!expect(fullscreenExitSignalCount == 34 && restoreRequestCount == 34,
                QStringLiteral("连续 30 次进退不得重复或遗漏恢复信号。"))) {
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
