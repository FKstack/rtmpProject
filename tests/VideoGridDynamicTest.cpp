#include <QAction>
#include <QGridLayout>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include "ui/ConnectionDialog.h"
#include "ui/MainWindow.h"
#include "ui/VideoGridWidget.h"
#include "ui/VideoWidget.h"

class VideoGridDynamicTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void calculateGridDimensions_data();
    void calculateGridDimensions();
    void initialGridIsEmpty();
    void mainWindowShowsEmptyConnectionPage();
    void connectionDialogValidatesInput();
    void addAndRemoveWidgets();
    void addWidgetsToMaximum();
    void interactionStatesRejectReentry();
    void dynamicallyCreatedWidgetKeepsConnections();
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
