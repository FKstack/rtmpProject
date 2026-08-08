#include <QAction>
#include <QDockWidget>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QToolBar>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "media/LatestFrameMailbox.h"
#include "media/PlaybackTypes.h"
#include "media/VideoFrame.h"
#include "ui/ConnectionDialog.h"
#include "ui/FullscreenVideoWindow.h"
#include "ui/MainWindow.h"
#include "ui/VideoGridWidget.h"
#include "ui/VideoWidget.h"

namespace {

VideoFrame makeGridTestFrame(std::uint64_t sequence)
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
        width,
        height,
        VideoPixelFormat::Yuv420P8,
        planes,
        0,
        1,
        {1, 30},
        {
            VideoColorPrimaries::Bt709,
            VideoTransferFunction::Bt709,
            VideoMatrixCoefficients::Bt709,
            VideoColorRange::Limited,
        },
        sequence,
        1,
        0
    );
    Q_ASSERT(frame.has_value());
    return *frame;
}

} // namespace

class VideoGridDynamicTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void calculateGridDimensions_data();
    void calculateGridDimensions();
    void calculateMonitoringGridGeometry_data();
    void calculateMonitoringGridGeometry();
    void monitoringWallGeometryUsesFullHd();
    void initialGridIsEmpty();
    void mainWindowShowsEmptyConnectionPage();
    void connectionDialogValidatesInput();
    void addAndRemoveWidgets();
    void addWidgetsToMaximum();
    void interactionStatesRejectReentry();
    void dynamicallyCreatedWidgetKeepsConnections();
    void displayModeIsPerWidgetAndSurvivesGridOperations();
    void monitoringGridKeepsSixteenByNineViewports();
    void logDockDefaultsHiddenAndCanBeShown();
    void monitoringWallRoundTripRestoresWindowChrome();
    void transientWidgetVisibilityDoesNotSuppressSharedCanvas();
    void inCanvasFullscreenRoundTripUsesMainCanvas();
    void inCanvasFullscreenExitsOnStreamUnbind();
    void inCanvasFullscreenEscapeKeyExits();
    void removalReleasesCanvasBindingsForCpuBackend();
    void removalReleasesTexturesForOpenGlBackend();
    void reconnectWaitingStreamCanBeRemoved();
    void fullscreenStreamRemovalRequiresExitFirst();
    void mainWindowDisablesAddActionAtMaximum();

private:
    static void verifyLogicalLayout(VideoGridWidget &grid);
    static VideoWidget *addAndWait(
        VideoGridWidget &grid,
        const QString &name = {}
    );
};

void VideoGridDynamicTest::initTestCase()
{
    qRegisterMetaType<VideoWidget *>("VideoWidget*");
}

void VideoGridDynamicTest::calculateGridDimensions_data()
{
    QTest::addColumn<int>("count");
    QTest::addColumn<int>("rows");
    QTest::addColumn<int>("columns");

    const QVector<GridDimensions> expected {
        {1, 1}, {1, 2}, {2, 2}, {2, 2}, {2, 3}, {2, 3}, {3, 3}, {3, 3},
        {3, 3}, {3, 4}, {3, 4}, {3, 4}, {4, 4}, {4, 4}, {4, 4}, {4, 4},
    };
    for (int index = 0; index < expected.size(); ++index) {
        QTest::newRow(
            (QByteArray::number(index + 1) + "-widgets").constData()
        ) << index + 1 << expected.at(index).rows << expected.at(index).columns;
    }
    QTest::newRow("zero-empty") << 0 << 0 << 0;
    QTest::newRow("seventeen-invalid") << 17 << 0 << 0;
}

void VideoGridDynamicTest::calculateGridDimensions()
{
    QFETCH(int, count);
    QFETCH(int, rows);
    QFETCH(int, columns);
    const GridDimensions dimensions =
        VideoGridWidget::calculateGridDimensions(count);
    QCOMPARE(dimensions.rows, rows);
    QCOMPARE(dimensions.columns, columns);
}

void VideoGridDynamicTest::calculateMonitoringGridGeometry_data()
{
    QTest::addColumn<QSize>("availableSize");
    QTest::addColumn<int>("count");
    const QVector<QSize> sizes {
        {1280, 720}, {1920, 1080}, {1920, 1032}, {2560, 1440},
    };
    for (const QSize &size : sizes) {
        for (int count = 1; count <= 16; ++count) {
            const QByteArray name = QByteArray::number(size.width()) + "x" +
                                    QByteArray::number(size.height()) + "-" +
                                    QByteArray::number(count);
            QTest::newRow(name.constData()) << size << count;
        }
    }
}

void VideoGridDynamicTest::calculateMonitoringGridGeometry()
{
    QFETCH(QSize, availableSize);
    QFETCH(int, count);
    const GridDimensions dimensions =
        VideoGridWidget::calculateGridDimensions(count);
    const QSize chrome(16, 39);
    const MonitoringGridGeometry geometry =
        VideoGridWidget::calculateMonitoringGridGeometry(
            availableSize, dimensions, chrome
    );
    QVERIFY(geometry.isValid());
    const qreal widthError = qAbs(
        qreal(geometry.videoViewportSize.width()) -
        qreal(geometry.videoViewportSize.height()) * 16.0 / 9.0
    );
    const qreal heightError = qAbs(
        qreal(geometry.videoViewportSize.height()) -
        qreal(geometry.videoViewportSize.width()) * 9.0 / 16.0
    );
    QVERIFY(std::min(widthError, heightError) <= 0.51);
    QCOMPARE(geometry.cellSize, geometry.videoViewportSize + chrome);
    QCOMPARE(
        geometry.gridRect.size(),
        QSize(
            dimensions.columns * geometry.cellSize.width() +
                (dimensions.columns - 1) * 4,
            dimensions.rows * geometry.cellSize.height() +
                (dimensions.rows - 1) * 4
        )
    );
    QVERIFY(qAbs(geometry.layoutMargins.left() -
                 geometry.layoutMargins.right()) <= 1);
    QVERIFY(qAbs(geometry.layoutMargins.top() -
                 geometry.layoutMargins.bottom()) <= 1);
    const int maximumVideoWidth =
        (availableSize.width() - 8 - (dimensions.columns - 1) * 4) /
            dimensions.columns - chrome.width();
    const int maximumVideoHeight =
        (availableSize.height() - 8 - (dimensions.rows - 1) * 4) /
            dimensions.rows - chrome.height();
    QVERIFY(geometry.videoViewportSize.width() <= maximumVideoWidth);
    QVERIFY(geometry.videoViewportSize.height() <= maximumVideoHeight);
    QVERIFY(geometry.videoViewportSize.width() == maximumVideoWidth ||
            geometry.videoViewportSize.height() == maximumVideoHeight);
}

void VideoGridDynamicTest::monitoringWallGeometryUsesFullHd()
{
    const MonitoringGridGeometry geometry =
        VideoGridWidget::calculateMonitoringGridGeometry(
            QSize(1920, 1080), {4, 4}, QSize(2, 2), QMargins(), 0
        );
    QVERIFY(geometry.isValid());
    QVERIFY(geometry.layoutMargins.left() <= 8);
    QVERIFY(geometry.layoutMargins.right() <= 8);
    QVERIFY(geometry.layoutMargins.top() <= 1);
    QVERIFY(geometry.layoutMargins.bottom() <= 1);
    QCOMPARE(geometry.gridRect.height(), 1080);
}

void VideoGridDynamicTest::initialGridIsEmpty()
{
    VideoGridWidget grid;
    QCOMPARE(grid.videoWidgetCount(), 0);
    QCOMPARE(grid.gridDimensions().rows, 0);
    QCOMPARE(grid.gridDimensions().columns, 0);
    QVERIFY(grid.canAddVideoWidget());
    QVERIFY(grid.videoWidgetAt(0) == nullptr);
    verifyLogicalLayout(grid);
}

void VideoGridDynamicTest::mainWindowShowsEmptyConnectionPage()
{
    MainWindow mainWindow;
    QCOMPARE(mainWindow.videoWidgetCount(), 0);
    QVERIFY(mainWindow.primaryVideoWidget() == nullptr);
    QVERIFY(mainWindow.videoWidgetAt(0) == nullptr);

    auto *emptyButton = mainWindow.findChild<QPushButton *>(
        QStringLiteral("emptyAddConnectionButton")
    );
    auto *addAction = mainWindow.findChild<QAction *>(
        QStringLiteral("addConnectionAction")
    );
    QVERIFY(emptyButton != nullptr);
    QVERIFY(emptyButton->isVisibleTo(&mainWindow));
    QVERIFY(addAction != nullptr);
    QVERIFY(addAction->isEnabled());

    QSignalSpy requestSpy(&mainWindow, &MainWindow::addConnectionRequested);
    addAction->trigger();
    QCOMPARE(requestSpy.count(), 1);
}

void VideoGridDynamicTest::connectionDialogValidatesInput()
{
    QVERIFY(ConnectionDialog::isValidRtmpUrl(
        QStringLiteral("rtmp://127.0.0.1:1935/live/camera001")
    ));
    QVERIFY(!ConnectionDialog::isValidRtmpUrl(QString()));
    QVERIFY(!ConnectionDialog::isValidRtmpUrl(
        QStringLiteral("https://127.0.0.1/live/camera001")
    ));
    QVERIFY(!ConnectionDialog::isValidRtmpUrl(
        QStringLiteral("rtmp:///live/camera001")
    ));
}

void VideoGridDynamicTest::addAndRemoveWidgets()
{
    VideoGridWidget grid;
    VideoWidget *first = grid.addVideoWidget(QStringLiteral("Lobby"));
    VideoWidget *second = grid.addVideoWidget(QStringLiteral("Door"));
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);
    QCOMPARE(grid.videoWidgetCount(), 2);
    QCOMPARE(first->deviceName(), QStringLiteral("Lobby"));
    QCOMPARE(second->deviceName(), QStringLiteral("Door"));

    QSignalSpy removedSpy(&grid, &VideoGridWidget::videoWidgetRemoved);
    QVERIFY(grid.removeVideoWidget(first));
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(grid.videoWidgetCount(), 1);
    QCOMPARE(grid.videoWidgetAt(0), second);
    QVERIFY(!grid.removeVideoWidget(first));
    QVERIFY(grid.removeVideoWidget(second));
    QCOMPARE(grid.videoWidgetCount(), 0);
}

void VideoGridDynamicTest::addWidgetsToMaximum()
{
    VideoGridWidget grid;
    grid.resize(1280, 720);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    QSet<VideoWidget *> uniqueWidgets;
    QSignalSpy maximumSpy(
        &grid, &VideoGridWidget::maximumVideoWidgetCountReached
    );
    for (int expectedCount = 1;
         expectedCount <= VideoGridWidget::kMaximumVideoWidgetCount;
         ++expectedCount) {
        VideoWidget *addedWidget = addAndWait(grid);
        QVERIFY(addedWidget != nullptr);
        QCOMPARE(addedWidget->displayMode(), VideoDisplayMode::Contain);
        QCOMPARE(grid.videoWidgetCount(), expectedCount);
        uniqueWidgets.insert(addedWidget);
        QCOMPARE(uniqueWidgets.size(), expectedCount);
        verifyLogicalLayout(grid);
    }
    QVERIFY(!grid.canAddVideoWidget());
    QCOMPARE(maximumSpy.count(), 1);
    QVERIFY(grid.addVideoWidget() == nullptr);
    QCOMPARE(maximumSpy.count(), 2);
}

void VideoGridDynamicTest::interactionStatesRejectReentry()
{
    VideoGridWidget grid;
    grid.resize(960, 540);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    VideoWidget *first = addAndWait(grid);
    QSignalSpy addedSpy(&grid, &VideoGridWidget::videoWidgetAdded);
    VideoWidget *second = grid.addVideoWidget();
    QVERIFY(second != nullptr);
    QCOMPARE(
        grid.interactionState(),
        VideoGridWidget::GridInteractionState::AddingWidget
    );
    QVERIFY(grid.addVideoWidget() == nullptr);
    QVERIFY(!grid.swapVideoWidgets(0, 1));
    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 1000);

    QSignalSpy swappedSpy(&grid, &VideoGridWidget::videoWidgetsSwapped);
    QVERIFY(grid.swapVideoWidgets(0, 1));
    QVERIFY(!grid.removeVideoWidget(first));
    QTRY_COMPARE_WITH_TIMEOUT(swappedSpy.count(), 1, 1000);

    VideoWidget *fullscreenWidget = grid.videoWidgetAt(0);
    QSignalSpy fullscreenSpy(&grid, &VideoGridWidget::fullscreenRequested);
    QTest::mouseDClick(fullscreenWidget, Qt::LeftButton);
    QCOMPARE(fullscreenSpy.count(), 1);
    QVERIFY(!grid.removeVideoWidget(fullscreenWidget));
    grid.notifyFullscreenEntryResult(fullscreenWidget, true);
    grid.notifyFullscreenExitStarted(fullscreenWidget);
    grid.notifyFullscreenExited(fullscreenWidget);
    QCOMPARE(
        grid.interactionState(),
        VideoGridWidget::GridInteractionState::Idle
    );
    QVERIFY(first != nullptr);
}

void VideoGridDynamicTest::dynamicallyCreatedWidgetKeepsConnections()
{
    VideoGridWidget grid;
    grid.resize(960, 540);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    VideoWidget *first = grid.addVideoWidget(QStringLiteral("First"));
    QVERIFY(first != nullptr);
    QTest::qWait(300);
    VideoWidget *second = grid.addVideoWidget(QStringLiteral("Second"));
    QVERIFY(second != nullptr);
    QTest::qWait(300);

    QSignalSpy fullscreenSpy(&grid, &VideoGridWidget::fullscreenRequested);
    second->fullscreenRequested(second);
    QCOMPARE(fullscreenSpy.count(), 1);
    grid.notifyFullscreenEntryResult(second, false);

    QSignalSpy swappedSpy(&grid, &VideoGridWidget::videoWidgetsSwapped);
    second->swapRequested(second, first);
    QTRY_COMPARE_WITH_TIMEOUT(swappedSpy.count(), 1, 1000);
    QCOMPARE(grid.videoWidgetAt(0), second);
    QCOMPARE(grid.videoWidgetAt(1), first);
}

void VideoGridDynamicTest::displayModeIsPerWidgetAndSurvivesGridOperations()
{
    VideoGridWidget grid(RendererPreference::Cpu);
    grid.resize(960, 540);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    VideoWidget *first = addAndWait(grid, QStringLiteral("First"));
    VideoWidget *second = addAndWait(grid, QStringLiteral("Second"));
    QCOMPARE(first->displayMode(), VideoDisplayMode::Contain);
    QCOMPARE(second->displayMode(), VideoDisplayMode::Contain);

    QSignalSpy renderStateSpy(first, &VideoWidget::renderStateChanged);
    first->setDisplayMode(VideoDisplayMode::Cover);
    QCOMPARE(first->displayMode(), VideoDisplayMode::Cover);
    QCOMPARE(renderStateSpy.count(), 1);
    first->setDisplayMode(VideoDisplayMode::Cover);
    QCOMPARE(renderStateSpy.count(), 1);
    QCOMPARE(second->displayMode(), VideoDisplayMode::Contain);

    auto mailbox = std::make_shared<LatestFrameMailbox>();
    grid.bindVideoStream(first, 41, mailbox);
    auto *canvas = grid.findChild<VideoCanvasHost *>(
        QStringLiteral("videoGridCanvas")
    );
    QVERIFY(canvas != nullptr);
    QCOMPARE(canvas->controller()->snapshot().items.size(), std::size_t(1));
    QCOMPARE(
        canvas->controller()->snapshot().items.front().displayMode,
        VideoDisplayMode::Cover
    );
    grid.setMonitoringWallMode(true);
    QVERIFY(grid.isMonitoringWallMode());
    QCOMPARE(first->displayMode(), VideoDisplayMode::Cover);
    QCOMPARE(
        canvas->controller()->snapshot().items.front().displayMode,
        VideoDisplayMode::Contain
    );
    grid.setMonitoringWallMode(false);
    QCOMPARE(first->displayMode(), VideoDisplayMode::Cover);
    QCOMPARE(
        canvas->controller()->snapshot().items.front().displayMode,
        VideoDisplayMode::Cover
    );
    first->showFrame();
    first->clearFrame();
    first->showFrame();
    QCOMPARE(first->displayMode(), VideoDisplayMode::Cover);

    QSignalSpy swappedSpy(&grid, &VideoGridWidget::videoWidgetsSwapped);
    QVERIFY(grid.swapVideoWidgets(0, 1));
    QTRY_COMPARE_WITH_TIMEOUT(swappedSpy.count(), 1, 1000);
    QCOMPARE(grid.videoWidgetAt(1), first);
    QCOMPARE(grid.videoWidgetAt(1)->displayMode(), VideoDisplayMode::Cover);
    QCOMPARE(grid.videoWidgetAt(0), second);
    QCOMPARE(grid.videoWidgetAt(0)->displayMode(), VideoDisplayMode::Contain);

    grid.unbindVideoStream(first);
    grid.bindVideoStream(first, 42, std::make_shared<LatestFrameMailbox>());
    QCOMPARE(first->displayMode(), VideoDisplayMode::Cover);
}

void VideoGridDynamicTest::monitoringGridKeepsSixteenByNineViewports()
{
    VideoGridWidget grid(RendererPreference::Cpu);
    grid.resize(1280, 720);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    for (int index = 0; index < 16; ++index) {
        QVERIFY(addAndWait(
            grid,
            index == 0
                ? QString(400, QLatin1Char('W'))
                : QStringLiteral("Camera %1").arg(index + 1)
        ) != nullptr);
    }

    const QVector<QSize> sizes {
        {1280, 720}, {1920, 1032},
    };
    for (const QSize &size : sizes) {
        grid.resize(size);
        QCoreApplication::processEvents();
        const MonitoringGridGeometry geometry = grid.monitoringGridGeometry();
        QVERIFY(geometry.isValid());
        QVERIFY(qAbs(geometry.layoutMargins.left() -
                     geometry.layoutMargins.right()) <= 1);
        QVERIFY(qAbs(geometry.layoutMargins.top() -
                     geometry.layoutMargins.bottom()) <= 1);
        for (int index = 0; index < 16; ++index) {
            VideoWidget *widget = grid.videoWidgetAt(index);
            QVERIFY(widget != nullptr);
            const QRect viewport = widget->videoViewportRect(&grid);
            QCOMPARE(viewport.size(), geometry.videoViewportSize);
            const qreal widthError = qAbs(
                qreal(viewport.width()) - qreal(viewport.height()) * 16.0 / 9.0
            );
            const qreal heightError = qAbs(
                qreal(viewport.height()) - qreal(viewport.width()) * 9.0 / 16.0
            );
            QVERIFY(std::min(widthError, heightError) <= 0.51);
            const VideoPlacement placement = calculateVideoPlacement(
                QRectF(viewport), QSize(1280, 720), VideoDisplayMode::Contain
            );
            QVERIFY(qAbs(placement.targetRect.left() - viewport.left()) <= 1.0);
            QVERIFY(qAbs(placement.targetRect.right() - viewport.right()) <= 1.0);
            QVERIFY(qAbs(placement.targetRect.top() - viewport.top()) <= 1.0);
            QVERIFY(qAbs(placement.targetRect.bottom() - viewport.bottom()) <= 1.0);
            QCOMPARE(placement.sourceUv, QRectF(0, 0, 1, 1));
        }
    }

    VideoWidget *first = grid.videoWidgetAt(0);
    auto *surface = first->findChild<QFrame *>(QStringLiteral("videoSurface"));
    auto *title = first->findChild<QLabel *>(QStringLiteral("deviceNameLabel"));
    QVERIFY(surface != nullptr);
    QVERIFY(title != nullptr);
    QCOMPARE(title->parentWidget(), surface);
    QCOMPARE(first->deviceName(), QString(400, QLatin1Char('W')));
    QVERIFY(title->text() != first->deviceName());
    QVERIFY(surface->rect().contains(title->geometry()));
    QVERIFY(first->videoChromeSizeHint().height() <= 4);
}

void VideoGridDynamicTest::logDockDefaultsHiddenAndCanBeShown()
{
    MainWindow mainWindow(RendererPreference::Cpu);
    mainWindow.show();
    auto *dock = mainWindow.findChild<QDockWidget *>(
        QStringLiteral("logDockWidget")
    );
    auto *action = mainWindow.findChild<QAction *>(
        QStringLiteral("showLogAction")
    );
    QVERIFY(dock != nullptr);
    QVERIFY(action != nullptr);
    QVERIFY(!dock->isVisible());
    QVERIFY(!action->isChecked());

    action->trigger();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(action->isChecked());
    action->trigger();
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(!action->isChecked());
}

void VideoGridDynamicTest::monitoringWallRoundTripRestoresWindowChrome()
{
    MainWindow mainWindow(RendererPreference::Cpu);
    VideoWidget *videoWidget = mainWindow.addConnectionWidget(
        QStringLiteral("Camera Wall Round Trip")
    );
    QVERIFY(videoWidget != nullptr);
    mainWindow.bindVideoStream(
        videoWidget, 77, std::make_shared<LatestFrameMailbox>()
    );
    videoWidget->showFrame();
    mainWindow.resize(1000, 700);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    auto *action = mainWindow.findChild<QAction *>(
        QStringLiteral("monitoringWallAction")
    );
    auto *toolbar = mainWindow.findChild<QToolBar *>(
        QStringLiteral("videoToolBar")
    );
    auto *dock = mainWindow.logDockWidget();
    QStatusBar *status = mainWindow.statusBar();
    QVERIFY(action != nullptr);
    QVERIFY(toolbar != nullptr);
    dock->show();
    status->show();
    QCoreApplication::processEvents();
    const QSize originalSize = mainWindow.size();

    action->trigger();
    QTRY_VERIFY(mainWindow.isMonitoringWallMode());
    QTRY_VERIFY(mainWindow.isFullScreen());
    QVERIFY(action->isChecked());
    QVERIFY(!mainWindow.menuBar()->isVisible());
    QVERIFY(!toolbar->isVisible());
    QVERIFY(!dock->isVisible());
    QVERIFY(!status->isVisible());
    auto *grid = mainWindow.findChild<VideoGridWidget *>();
    QVERIFY(grid != nullptr);
    QVERIFY(grid->isMonitoringWallMode());

    QTest::mouseDClick(videoWidget, Qt::LeftButton);
    auto *fullscreen = mainWindow.findChild<FullscreenVideoWindow *>();
    QVERIFY(fullscreen != nullptr);
    QTRY_VERIFY(fullscreen->isFullscreenActive());
    fullscreen->exitFullscreen();
    QTRY_VERIFY(!fullscreen->isFullscreenActive());
    QTRY_VERIFY(mainWindow.isVisible());
    QTRY_VERIFY(mainWindow.isFullScreen());
    QVERIFY(mainWindow.isMonitoringWallMode());

    QTest::keyClick(&mainWindow, Qt::Key_Escape);
    QTRY_VERIFY(!mainWindow.isMonitoringWallMode());
    QTRY_VERIFY(!mainWindow.isFullScreen());
    QVERIFY(!action->isChecked());
    QVERIFY(mainWindow.menuBar()->isVisible());
    QVERIFY(toolbar->isVisible());
    QVERIFY(dock->isVisible());
    QVERIFY(status->isVisible());
    QVERIFY(!grid->isMonitoringWallMode());
    QVERIFY(qAbs(mainWindow.width() - originalSize.width()) <= 2);
    QVERIFY(qAbs(mainWindow.height() - originalSize.height()) <= 2);
}

void VideoGridDynamicTest::transientWidgetVisibilityDoesNotSuppressSharedCanvas()
{
    VideoGridWidget grid(RendererPreference::Cpu);
    grid.resize(960, 540);

    std::vector<std::shared_ptr<LatestFrameMailbox>> mailboxes;
    for (StreamId streamId = 1; streamId <= 4; ++streamId) {
        VideoWidget *videoWidget = grid.addVideoWidget(
            QStringLiteral("Camera %1").arg(streamId, 2, 10, QLatin1Char('0'))
        );
        QVERIFY(videoWidget != nullptr);
        auto mailbox = std::make_shared<LatestFrameMailbox>();
        grid.bindVideoStream(videoWidget, streamId, mailbox);

        // Reproduce the preload/layout-animation window from the reported bug:
        // the interaction anchor is temporarily hidden when Playing arrives.
        videoWidget->hide();
        videoWidget->showFrame();
        QVERIFY(mailbox->submit(makeGridTestFrame(streamId)));
        mailboxes.push_back(std::move(mailbox));
    }

    // No explicit refresh is allowed here: showFrame() must update the shared
    // canvas snapshot through VideoWidget::renderStateChanged by itself.
    RenderRuntimeMetrics metrics = grid.rendererRuntimeMetrics();
    QCOMPARE(metrics.renderItemCount, 4);
    QCOMPARE(metrics.boundMailboxCount, 4);
    QCOMPARE(metrics.visibleRenderItemCount, 4);

    for (int index = 0; index < grid.videoWidgetCount(); ++index) {
        grid.videoWidgetAt(index)->show();
    }
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    QTRY_VERIFY_WITH_TIMEOUT(
        grid.rendererRuntimeMetrics().uploadedFrames >= 4, 5'000
    );
    QTRY_VERIFY_WITH_TIMEOUT(
        grid.rendererRuntimeMetrics().renderedFrames >= 4, 5'000
    );
}

void VideoGridDynamicTest::inCanvasFullscreenRoundTripUsesMainCanvas()
{
    VideoGridWidget grid(RendererPreference::Cpu);
    grid.resize(960, 540);

    std::vector<std::shared_ptr<LatestFrameMailbox>> mailboxes;
    for (StreamId streamId = 1; streamId <= 2; ++streamId) {
        VideoWidget *videoWidget = grid.addVideoWidget(
            QStringLiteral("Camera %1").arg(streamId, 2, 10, QLatin1Char('0'))
        );
        QVERIFY(videoWidget != nullptr);
        auto mailbox = std::make_shared<LatestFrameMailbox>();
        grid.bindVideoStream(videoWidget, streamId, mailbox);
        videoWidget->showFrame();
        QVERIFY(mailbox->submit(makeGridTestFrame(streamId)));
        mailboxes.push_back(std::move(mailbox));
    }
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 2);

    VideoWidget *target = grid.videoWidgetAt(0);
    target->fullscreenRequested(target);
    QCOMPARE(grid.interactionState(),
             VideoGridWidget::GridInteractionState::EnteringFullscreen);

    // EGLFS 单窗口模式：主画布直接切单路 Snapshot，不创建第二个画布。
    QVERIFY(grid.enterInCanvasFullscreen(target));
    grid.notifyFullscreenEntryResult(target, true);
    QVERIFY(grid.isInCanvasFullscreenActive());
    QCOMPARE(grid.interactionState(),
             VideoGridWidget::GridInteractionState::Fullscreen);
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 1);
    QVERIFY(grid.videoWidgetAt(0)->isHidden());
    QVERIFY(grid.videoWidgetAt(1)->isHidden());

    // 断流标记通过 renderStateChanged 刷新单路 Snapshot，不会退回网格。
    target->clearFrame();
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 1);
    QCOMPARE(grid.rendererRuntimeMetrics().visibleRenderItemCount, 0);

    grid.exitInCanvasFullscreen();
    QVERIFY(!grid.isInCanvasFullscreenActive());
    QCOMPARE(grid.interactionState(),
             VideoGridWidget::GridInteractionState::Idle);
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 2);
    QVERIFY(!grid.videoWidgetAt(0)->isHidden());
    QVERIFY(!grid.videoWidgetAt(1)->isHidden());
}

void VideoGridDynamicTest::inCanvasFullscreenExitsOnStreamUnbind()
{
    VideoGridWidget grid(RendererPreference::Cpu);
    grid.resize(960, 540);

    VideoWidget *first = grid.addVideoWidget(QStringLiteral("Camera 01"));
    VideoWidget *second = grid.addVideoWidget(QStringLiteral("Camera 02"));
    QVERIFY(first != nullptr && second != nullptr);
    auto firstMailbox = std::make_shared<LatestFrameMailbox>();
    auto secondMailbox = std::make_shared<LatestFrameMailbox>();
    grid.bindVideoStream(first, 1, firstMailbox);
    grid.bindVideoStream(second, 2, secondMailbox);

    first->fullscreenRequested(first);
    QVERIFY(grid.enterInCanvasFullscreen(first));
    grid.notifyFullscreenEntryResult(first, true);
    QVERIFY(grid.isInCanvasFullscreenActive());

    // 解绑当前画布内全屏的流必须自动退出全屏并恢复网格。
    grid.unbindVideoStream(first);
    QVERIFY(!grid.isInCanvasFullscreenActive());
    QCOMPARE(grid.interactionState(),
             VideoGridWidget::GridInteractionState::Idle);
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 1);
    QVERIFY(!second->isHidden());
}

void VideoGridDynamicTest::inCanvasFullscreenEscapeKeyExits()
{
    VideoGridWidget grid(RendererPreference::Cpu);
    grid.resize(960, 540);

    VideoWidget *widget = grid.addVideoWidget(QStringLiteral("Camera 01"));
    QVERIFY(widget != nullptr);
    auto mailbox = std::make_shared<LatestFrameMailbox>();
    grid.bindVideoStream(widget, 1, mailbox);

    widget->fullscreenRequested(widget);
    QVERIFY(grid.enterInCanvasFullscreen(widget));
    grid.notifyFullscreenEntryResult(widget, true);
    QVERIFY(grid.isInCanvasFullscreenActive());

    QTest::keyClick(&grid, Qt::Key_Escape);
    QVERIFY(!grid.isInCanvasFullscreenActive());
    QCOMPARE(grid.interactionState(),
             VideoGridWidget::GridInteractionState::Idle);
}

void VideoGridDynamicTest::removalReleasesCanvasBindingsForCpuBackend()
{
    VideoGridWidget grid(RendererPreference::Cpu);
    grid.resize(960, 540);

    VideoWidget *first = grid.addVideoWidget(QStringLiteral("Camera 01"));
    VideoWidget *second = grid.addVideoWidget(QStringLiteral("Camera 02"));
    QVERIFY(first != nullptr && second != nullptr);
    auto firstMailbox = std::make_shared<LatestFrameMailbox>();
    auto secondMailbox = std::make_shared<LatestFrameMailbox>();
    grid.bindVideoStream(first, 1, firstMailbox);
    grid.bindVideoStream(second, 2, secondMailbox);
    first->showFrame();
    second->showFrame();
    QVERIFY(firstMailbox->submit(makeGridTestFrame(1)));
    QVERIFY(secondMailbox->submit(makeGridTestFrame(2)));

    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 2);
    QCOMPARE(grid.rendererRuntimeMetrics().boundMailboxCount, 2);

    // 播放中删除：Widget、Snapshot 项与 mailbox 绑定同时释放。
    QVERIFY(grid.removeVideoWidget(first));
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 1);
    QCOMPARE(grid.rendererRuntimeMetrics().boundMailboxCount, 1);

    // 最后一路删除：画布不再持有任何 Snapshot 项或 mailbox 绑定。
    QVERIFY(grid.removeVideoWidget(second));
    QCOMPARE(grid.videoWidgetCount(), 0);
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 0);
    QCOMPARE(grid.rendererRuntimeMetrics().boundMailboxCount, 0);
}

void VideoGridDynamicTest::removalReleasesTexturesForOpenGlBackend()
{
    VideoGridWidget grid(RendererPreference::OpenGL);
    grid.resize(640, 360);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));
    if (grid.activeRendererBackend() != QStringLiteral("opengl")) {
        QSKIP("OpenGL backend unavailable in this environment.");
    }

    VideoWidget *widget = grid.addVideoWidget(QStringLiteral("Camera 01"));
    QVERIFY(widget != nullptr);
    auto mailbox = std::make_shared<LatestFrameMailbox>();
    grid.bindVideoStream(widget, 1, mailbox);
    widget->showFrame();
    QVERIFY(mailbox->submit(makeGridTestFrame(1)));

    QTRY_VERIFY_WITH_TIMEOUT(
        grid.rendererRuntimeMetrics().textureBytes > 0, 5'000
    );
    // 可见网格的添加动画结束后才允许删除。
    QTRY_VERIFY_WITH_TIMEOUT(
        grid.interactionState() == VideoGridWidget::GridInteractionState::Idle,
        5'000
    );

    QVERIFY(grid.removeVideoWidget(widget));
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 0);
    QCOMPARE(grid.rendererRuntimeMetrics().boundMailboxCount, 0);
    // 删除后纹理必须随 Snapshot 项消失而释放，下一次绘制即归零。
    QTRY_VERIFY_WITH_TIMEOUT(
        grid.rendererRuntimeMetrics().textureBytes == 0, 5'000
    );
}

void VideoGridDynamicTest::reconnectWaitingStreamCanBeRemoved()
{
    MainWindow mainWindow;
    VideoWidget *videoWidget =
        mainWindow.addConnectionWidget(QStringLiteral("Camera 01"));
    QVERIFY(videoWidget != nullptr);
    auto mailbox = std::make_shared<LatestFrameMailbox>();
    mainWindow.bindVideoStream(videoWidget, 1, mailbox);

    // 重连等待状态：连接已断开但格子仍在等待自动重连。
    mainWindow.updateDeviceStatus(videoWidget, DeviceStatus::Reconnecting);
    QVERIFY(mainWindow.removeConnectionWidget(videoWidget));
    QCOMPARE(mainWindow.videoWidgetCount(), 0);
    QCOMPARE(mainWindow.rendererRuntimeMetrics().renderItemCount, 0);
    QCOMPARE(mainWindow.rendererRuntimeMetrics().boundMailboxCount, 0);
}

void VideoGridDynamicTest::fullscreenStreamRemovalRequiresExitFirst()
{
    VideoGridWidget grid(RendererPreference::Cpu);
    grid.resize(960, 540);

    VideoWidget *first = grid.addVideoWidget(QStringLiteral("Camera 01"));
    QVERIFY(first != nullptr);
    auto mailbox = std::make_shared<LatestFrameMailbox>();
    grid.bindVideoStream(first, 1, mailbox);
    first->showFrame();
    QVERIFY(mailbox->submit(makeGridTestFrame(1)));

    first->fullscreenRequested(first);
    QVERIFY(grid.enterInCanvasFullscreen(first));
    grid.notifyFullscreenEntryResult(first, true);
    QVERIFY(grid.isInCanvasFullscreenActive());

    // 全屏期间删除被拒绝；退出全屏后删除成功并释放全部绑定。
    QVERIFY(!grid.removeVideoWidget(first));
    grid.exitInCanvasFullscreen();
    QVERIFY(grid.removeVideoWidget(first));
    QCOMPARE(grid.rendererRuntimeMetrics().renderItemCount, 0);
    QCOMPARE(grid.rendererRuntimeMetrics().boundMailboxCount, 0);
}

void VideoGridDynamicTest::mainWindowDisablesAddActionAtMaximum()
{
    MainWindow mainWindow;
    auto *addAction = mainWindow.findChild<QAction *>(
        QStringLiteral("addConnectionAction")
    );
    QVERIFY(addAction != nullptr);

    for (int index = 1; index <= 16; ++index) {
        QVERIFY(mainWindow.addConnectionWidget(
                    QStringLiteral("Camera %1")
                        .arg(index, 2, 10, QLatin1Char('0'))
                ) != nullptr);
    }
    QCOMPARE(mainWindow.videoWidgetCount(), 16);
    QVERIFY(!addAction->isEnabled());
    QVERIFY(addAction->toolTip().contains(QStringLiteral("16")));
}

void VideoGridDynamicTest::verifyLogicalLayout(VideoGridWidget &grid)
{
    auto *layout = qobject_cast<QGridLayout *>(grid.layout());
    QVERIFY(layout != nullptr);
    QCOMPARE(layout->count(), grid.videoWidgetCount());
    const GridDimensions dimensions = grid.gridDimensions();
    if (grid.videoWidgetCount() == 0) {
        QCOMPARE(dimensions.rows, 0);
        QCOMPARE(dimensions.columns, 0);
        return;
    }

    QSet<VideoWidget *> uniqueWidgets;
    for (int index = 0; index < grid.videoWidgetCount(); ++index) {
        VideoWidget *videoWidget = grid.videoWidgetAt(index);
        QVERIFY(videoWidget != nullptr);
        QVERIFY(!uniqueWidgets.contains(videoWidget));
        uniqueWidgets.insert(videoWidget);

        const int layoutIndex = layout->indexOf(videoWidget);
        QVERIFY(layoutIndex >= 0);
        int row = -1;
        int column = -1;
        int rowSpan = 0;
        int columnSpan = 0;
        layout->getItemPosition(
            layoutIndex, &row, &column, &rowSpan, &columnSpan
        );
        QCOMPARE(row, index / dimensions.columns);
        QCOMPARE(column, index % dimensions.columns);
        QCOMPARE(rowSpan, 1);
        QCOMPARE(columnSpan, 1);
    }
}

VideoWidget *VideoGridDynamicTest::addAndWait(
    VideoGridWidget &grid,
    const QString &name
)
{
    QSignalSpy addedSpy(&grid, &VideoGridWidget::videoWidgetAdded);
    VideoWidget *videoWidget = name.isEmpty()
                                   ? grid.addVideoWidget()
                                   : grid.addVideoWidget(name);
    if (videoWidget != nullptr && addedSpy.count() == 0) {
        QTest::qWait(300);
    }
    return videoWidget;
}

QTEST_MAIN(VideoGridDynamicTest)

#include "VideoGridDynamicTest.moc"
