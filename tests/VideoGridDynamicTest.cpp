#include <QGridLayout>
#include <QAction>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include "ui/VideoGridWidget.h"
#include "ui/VideoWidget.h"
#include "ui/MainWindow.h"

/**
 * @brief 验证动态视频网格的纯布局算法、数量边界和交互状态。
 *
 * 测试运行在 Qt UI 线程；动画通过 QTRY_* 宏等待信号和状态恢复，不主动调用
 * processEvents() 干预产品代码时序。
 */
class VideoGridDynamicTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void calculateGridDimensions_data();
    void calculateGridDimensions();
    void initialGridContainsOneWidget();
    void mainWindowContainsFourPlaybackWidgets();
    void addWidgetsToMaximum();
    void interactionStatesRejectReentry();
    void dynamicallyCreatedWidgetKeepsConnections();
    void mainWindowDisablesAddActionAtMaximum();

private:
    static void verifyLogicalLayout(VideoGridWidget &grid);
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
        const QByteArray rowName = QByteArray::number(index + 1) + "-widgets";
        QTest::newRow(rowName.constData())
            << index + 1 << expected.at(index).rows << expected.at(index).columns;
    }

    QTest::newRow("zero-invalid") << 0 << 0 << 0;
    QTest::newRow("seventeen-invalid") << 17 << 0 << 0;
}

void VideoGridDynamicTest::calculateGridDimensions()
{
    QFETCH(int, count);
    QFETCH(int, rows);
    QFETCH(int, columns);

    const GridDimensions dimensions = VideoGridWidget::calculateGridDimensions(count);
    QCOMPARE(dimensions.rows, rows);
    QCOMPARE(dimensions.columns, columns);
}

void VideoGridDynamicTest::initialGridContainsOneWidget()
{
    VideoGridWidget grid;

    QCOMPARE(grid.videoWidgetCount(), 1);
    QCOMPARE(grid.gridDimensions().rows, 1);
    QCOMPARE(grid.gridDimensions().columns, 1);
    QVERIFY(grid.canAddVideoWidget());
    QCOMPARE(grid.interactionState(), VideoGridWidget::GridInteractionState::Idle);
    QVERIFY(grid.videoWidgetAt(0) != nullptr);
    QCOMPARE(grid.videoWidgetAt(0)->deviceName(), QStringLiteral("Camera 01"));
    verifyLogicalLayout(grid);
}

void VideoGridDynamicTest::mainWindowContainsFourPlaybackWidgets()
{
    MainWindow mainWindow;
    auto *grid = mainWindow.findChild<VideoGridWidget *>();

    QVERIFY(grid != nullptr);
    QCOMPARE(grid->videoWidgetCount(), MainWindow::kInitialPlaybackWidgetCount);
    QCOMPARE(grid->gridDimensions().rows, 2);
    QCOMPARE(grid->gridDimensions().columns, 2);
    QCOMPARE(mainWindow.primaryVideoWidget(), mainWindow.videoWidgetAt(0));
    QVERIFY(mainWindow.videoWidgetAt(-1) == nullptr);
    QVERIFY(mainWindow.videoWidgetAt(MainWindow::kInitialPlaybackWidgetCount) == nullptr);

    for (int index = 0; index < MainWindow::kInitialPlaybackWidgetCount; ++index) {
        VideoWidget *videoWidget = mainWindow.videoWidgetAt(index);
        QVERIFY(videoWidget != nullptr);
        QCOMPARE(
            videoWidget->deviceName(),
            QStringLiteral("Camera %1").arg(index + 1, 2, 10, QLatin1Char('0'))
        );
    }
}

void VideoGridDynamicTest::addWidgetsToMaximum()
{
    VideoGridWidget grid;
    grid.resize(1280, 720);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    QSet<VideoWidget *> uniqueWidgets;
    uniqueWidgets.insert(grid.videoWidgetAt(0));
    QSet<QString> uniqueNames;
    uniqueNames.insert(grid.videoWidgetAt(0)->deviceName());
    QSignalSpy maximumSpy(&grid, &VideoGridWidget::maximumVideoWidgetCountReached);

    for (int expectedCount = 2;
         expectedCount <= VideoGridWidget::kMaximumVideoWidgetCount;
         ++expectedCount) {
        QSignalSpy addedSpy(&grid, &VideoGridWidget::videoWidgetAdded);
        VideoWidget *addedWidget = grid.addVideoWidget();

        QVERIFY(addedWidget != nullptr);
        QCOMPARE(grid.videoWidgetCount(), expectedCount);
        QCOMPARE(grid.interactionState(),
                 VideoGridWidget::GridInteractionState::AddingWidget);
        QVERIFY(!grid.canAddVideoWidget());
        QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 1000);
        QCOMPARE(grid.interactionState(), VideoGridWidget::GridInteractionState::Idle);

        uniqueWidgets.insert(addedWidget);
        QCOMPARE(uniqueWidgets.size(), expectedCount);
        uniqueNames.insert(addedWidget->deviceName());
        QCOMPARE(uniqueNames.size(), expectedCount);
        QCOMPARE(addedWidget->deviceName(),
                 QStringLiteral("Camera %1").arg(expectedCount, 2, 10, QLatin1Char('0')));
        verifyLogicalLayout(grid);
    }

    QVERIFY(!grid.canAddVideoWidget());
    QCOMPARE(maximumSpy.count(), 1);
    QVERIFY(grid.addVideoWidget() == nullptr);
    QCOMPARE(grid.videoWidgetCount(), VideoGridWidget::kMaximumVideoWidgetCount);
    QCOMPARE(maximumSpy.count(), 2);
}

void VideoGridDynamicTest::interactionStatesRejectReentry()
{
    VideoGridWidget grid;
    grid.resize(960, 540);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    QSignalSpy addedSpy(&grid, &VideoGridWidget::videoWidgetAdded);
    QVERIFY(grid.addVideoWidget() != nullptr);
    QCOMPARE(grid.interactionState(), VideoGridWidget::GridInteractionState::AddingWidget);
    QVERIFY(grid.addVideoWidget() == nullptr);
    QVERIFY(!grid.swapVideoWidgets(0, 1));
    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 1000);

    QSignalSpy swappedSpy(&grid, &VideoGridWidget::videoWidgetsSwapped);
    QVERIFY(grid.swapVideoWidgets(0, 1));
    QCOMPARE(grid.interactionState(), VideoGridWidget::GridInteractionState::SwappingWidgets);
    QVERIFY(grid.addVideoWidget() == nullptr);
    QVERIFY(!grid.swapVideoWidgets(0, 1));
    QTRY_COMPARE_WITH_TIMEOUT(swappedSpy.count(), 1, 1000);
    QCOMPARE(grid.interactionState(), VideoGridWidget::GridInteractionState::Idle);

    VideoWidget *fullscreenWidget = grid.videoWidgetAt(0);
    QSignalSpy fullscreenSpy(&grid, &VideoGridWidget::fullscreenRequested);
    QTest::mouseDClick(fullscreenWidget, Qt::LeftButton);
    QCOMPARE(fullscreenSpy.count(), 1);
    QCOMPARE(grid.interactionState(),
             VideoGridWidget::GridInteractionState::EnteringFullscreen);
    QVERIFY(grid.addVideoWidget() == nullptr);
    QVERIFY(!grid.swapVideoWidgets(0, 1));

    grid.notifyFullscreenEntryResult(fullscreenWidget, true);
    QCOMPARE(grid.interactionState(), VideoGridWidget::GridInteractionState::Fullscreen);
    QVERIFY(!fullscreenWidget->isDragEnabled());

    grid.notifyFullscreenExitStarted(fullscreenWidget);
    QCOMPARE(grid.interactionState(),
             VideoGridWidget::GridInteractionState::ExitingFullscreen);
    grid.notifyFullscreenExited(fullscreenWidget);
    QCOMPARE(grid.interactionState(), VideoGridWidget::GridInteractionState::Idle);
    QVERIFY(fullscreenWidget->isDragEnabled());
}

void VideoGridDynamicTest::dynamicallyCreatedWidgetKeepsConnections()
{
    VideoGridWidget grid;
    grid.resize(960, 540);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    QSignalSpy addedSpy(&grid, &VideoGridWidget::videoWidgetAdded);
    VideoWidget *newWidget = grid.addVideoWidget();
    QVERIFY(newWidget != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 1000);

    QSignalSpy fullscreenSpy(&grid, &VideoGridWidget::fullscreenRequested);
    QTest::mouseDClick(newWidget, Qt::LeftButton);
    QCOMPARE(fullscreenSpy.count(), 1);
    QCOMPARE(fullscreenSpy.at(0).at(0).value<VideoWidget *>(), newWidget);
    grid.notifyFullscreenEntryResult(newWidget, false);

    VideoWidget *originalFirstWidget = grid.videoWidgetAt(0);
    QSignalSpy swappedSpy(&grid, &VideoGridWidget::videoWidgetsSwapped);
    newWidget->swapRequested(newWidget, originalFirstWidget);
    QTRY_COMPARE_WITH_TIMEOUT(swappedSpy.count(), 1, 1000);
    QCOMPARE(grid.videoWidgetAt(0), newWidget);
    QCOMPARE(grid.videoWidgetAt(1), originalFirstWidget);
}

void VideoGridDynamicTest::mainWindowDisablesAddActionAtMaximum()
{
    MainWindow mainWindow;
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    auto *grid = mainWindow.findChild<VideoGridWidget *>();
    auto *addAction = mainWindow.findChild<QAction *>(
        QStringLiteral("addVideoWidgetAction")
    );
    QVERIFY(grid != nullptr);
    QVERIFY(addAction != nullptr);
    QVERIFY(addAction->isEnabled());
    QCOMPARE(grid->videoWidgetCount(), MainWindow::kInitialPlaybackWidgetCount);

    for (int expectedCount = MainWindow::kInitialPlaybackWidgetCount + 1;
         expectedCount <= VideoGridWidget::kMaximumVideoWidgetCount;
         ++expectedCount) {
        QSignalSpy addedSpy(grid, &VideoGridWidget::videoWidgetAdded);
        addAction->trigger();
        QVERIFY(!addAction->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 1000);
        QCOMPARE(grid->videoWidgetCount(), expectedCount);
        QCOMPARE(addAction->isEnabled(),
                 expectedCount < VideoGridWidget::kMaximumVideoWidgetCount);
    }

    QVERIFY(!addAction->isEnabled());
    QVERIFY(addAction->toolTip().contains(QStringLiteral("16")));
    addAction->trigger();
    QCOMPARE(grid->videoWidgetCount(), VideoGridWidget::kMaximumVideoWidgetCount);
}

void VideoGridDynamicTest::verifyLogicalLayout(VideoGridWidget &grid)
{
    auto *layout = qobject_cast<QGridLayout *>(grid.layout());
    QVERIFY(layout != nullptr);
    QCOMPARE(layout->count(), grid.videoWidgetCount());

    const GridDimensions dimensions = grid.gridDimensions();
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
        layout->getItemPosition(layoutIndex, &row, &column, &rowSpan, &columnSpan);
        QCOMPARE(row, index / dimensions.columns);
        QCOMPARE(column, index % dimensions.columns);
        QVERIFY(row < dimensions.rows);
        QVERIFY(column < dimensions.columns);
        QCOMPARE(rowSpan, 1);
        QCOMPARE(columnSpan, 1);
    }
}

QTEST_MAIN(VideoGridDynamicTest)

#include "VideoGridDynamicTest.moc"
