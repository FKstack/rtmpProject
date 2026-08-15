#include <QtTest>

#include <QDialog>
#include <QLineEdit>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/DeviceControlInputRouter.h"

class DeviceControlInputRouterTest final : public QObject
{
    Q_OBJECT
private slots:
    void requiresExplicitArming();
    void mapsKeysAndIgnoresAutoRepeat_data();
    void mapsKeysAndIgnoresAutoRepeat();
    void lastPressedDirectionWinsAndFallsBack();
    void spaceStopsAndClearsHeldKeys();
    void textEntryIsNotIntercepted();
    void escapeModeChangeAndDisconnectDisarm();
    void modalDialogAndApplicationDeactivationDisarm();
};

void DeviceControlInputRouterTest::requiresExplicitArming()
{
    QWidget window;
    window.show();
    DeviceControlInputRouter router(&window);
    QSignalSpy commandSpy(&router, &DeviceControlInputRouter::commandPressed);
    QTest::keyPress(&window, Qt::Key_W);
    QCOMPARE(commandSpy.count(), 0);

    router.setConnected(true);
    router.setKeyboardModeSelected(true);
    router.setKeyboardArmed(true);
    QVERIFY(router.keyboardArmed());
    QTest::keyPress(&window, Qt::Key_W);
    QCOMPARE(commandSpy.count(), 1);
}

void DeviceControlInputRouterTest::mapsKeysAndIgnoresAutoRepeat_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<DeviceCommand>("command");
    QTest::newRow("w") << int(Qt::Key_W) << DeviceCommand::MoveForward;
    QTest::newRow("up") << int(Qt::Key_Up) << DeviceCommand::MoveForward;
    QTest::newRow("s") << int(Qt::Key_S) << DeviceCommand::MoveBackward;
    QTest::newRow("down") << int(Qt::Key_Down) << DeviceCommand::MoveBackward;
    QTest::newRow("a") << int(Qt::Key_A) << DeviceCommand::TurnLeft;
    QTest::newRow("left") << int(Qt::Key_Left) << DeviceCommand::TurnLeft;
    QTest::newRow("d") << int(Qt::Key_D) << DeviceCommand::TurnRight;
    QTest::newRow("right") << int(Qt::Key_Right) << DeviceCommand::TurnRight;
}

void DeviceControlInputRouterTest::mapsKeysAndIgnoresAutoRepeat()
{
    QFETCH(int, key);
    QFETCH(DeviceCommand, command);
    QWidget window;
    window.show();
    DeviceControlInputRouter router(&window);
    router.setConnected(true);
    router.setKeyboardModeSelected(true);
    router.setKeyboardArmed(true);
    QSignalSpy commandSpy(&router, &DeviceControlInputRouter::commandPressed);

    QTest::keyPress(&window, static_cast<Qt::Key>(key));
    QCOMPARE(commandSpy.count(), 1);
    QCOMPARE(qvariant_cast<DeviceCommand>(commandSpy.at(0).at(0)), command);
    QKeyEvent repeat(QEvent::KeyPress, key, Qt::NoModifier,
                     QString(), true, 2);
    QCoreApplication::sendEvent(&window, &repeat);
    QCOMPARE(commandSpy.count(), 1);
}

void DeviceControlInputRouterTest::lastPressedDirectionWinsAndFallsBack()
{
    QWidget window;
    window.show();
    DeviceControlInputRouter router(&window);
    router.setConnected(true);
    router.setKeyboardModeSelected(true);
    router.setKeyboardArmed(true);
    QSignalSpy commandSpy(&router, &DeviceControlInputRouter::commandPressed);
    QSignalSpy stopSpy(&router, &DeviceControlInputRouter::movementReleased);

    QTest::keyPress(&window, Qt::Key_W);
    QTest::keyPress(&window, Qt::Key_D);
    QTest::keyRelease(&window, Qt::Key_D);
    QCOMPARE(commandSpy.count(), 3);
    QCOMPARE(qvariant_cast<DeviceCommand>(commandSpy.at(0).at(0)),
             DeviceCommand::MoveForward);
    QCOMPARE(qvariant_cast<DeviceCommand>(commandSpy.at(1).at(0)),
             DeviceCommand::TurnRight);
    QCOMPARE(qvariant_cast<DeviceCommand>(commandSpy.at(2).at(0)),
             DeviceCommand::MoveForward);
    QCOMPARE(stopSpy.count(), 0);
    QTest::keyRelease(&window, Qt::Key_W);
    QCOMPARE(stopSpy.count(), 1);
}

void DeviceControlInputRouterTest::spaceStopsAndClearsHeldKeys()
{
    QWidget window;
    window.show();
    DeviceControlInputRouter router(&window);
    router.setConnected(true);
    router.setKeyboardModeSelected(true);
    router.setKeyboardArmed(true);
    QSignalSpy commandSpy(&router, &DeviceControlInputRouter::commandPressed);
    QSignalSpy stopSpy(&router, &DeviceControlInputRouter::movementReleased);

    QTest::keyPress(&window, Qt::Key_W);
    QTest::keyPress(&window, Qt::Key_Space);
    QCOMPARE(commandSpy.count(), 2);
    QCOMPARE(qvariant_cast<DeviceCommand>(commandSpy.at(1).at(0)),
             DeviceCommand::StopCar);
    QCOMPARE(stopSpy.count(), 0);
    QTest::keyRelease(&window, Qt::Key_W);
    QCOMPARE(commandSpy.count(), 2);
    QCOMPARE(stopSpy.count(), 0);
}

void DeviceControlInputRouterTest::textEntryIsNotIntercepted()
{
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *edit = new QLineEdit(&window);
    layout->addWidget(edit);
    window.show();
    edit->setFocus();
    QTest::qWait(10);
    DeviceControlInputRouter router(&window);
    router.setConnected(true);
    router.setKeyboardModeSelected(true);
    router.setKeyboardArmed(true);
    QSignalSpy commandSpy(&router, &DeviceControlInputRouter::commandPressed);

    QTest::keyClicks(edit, QStringLiteral("wasd"));
    QCOMPARE(commandSpy.count(), 0);
    QCOMPARE(edit->text(), QStringLiteral("wasd"));
    QVERIFY(router.keyboardArmed());
}

void DeviceControlInputRouterTest::escapeModeChangeAndDisconnectDisarm()
{
    QWidget window;
    window.show();
    DeviceControlInputRouter router(&window);
    router.setConnected(true);
    router.setKeyboardModeSelected(true);
    router.setKeyboardArmed(true);
    QSignalSpy stopSpy(&router, &DeviceControlInputRouter::movementReleased);

    QTest::keyPress(&window, Qt::Key_W);
    QTest::keyPress(&window, Qt::Key_Escape);
    QVERIFY(!router.keyboardArmed());
    QCOMPARE(stopSpy.count(), 1);

    router.setKeyboardArmed(true);
    QVERIFY(router.keyboardArmed());
    QTest::keyPress(&window, Qt::Key_D);
    router.setKeyboardModeSelected(false);
    QVERIFY(!router.keyboardArmed());
    QCOMPARE(stopSpy.count(), 2);

    router.setKeyboardModeSelected(true);
    router.setKeyboardArmed(true);
    QVERIFY(router.keyboardArmed());
    router.setConnected(false);
    QVERIFY(!router.keyboardArmed());
}

void DeviceControlInputRouterTest::modalDialogAndApplicationDeactivationDisarm()
{
    QWidget window;
    window.show();
    DeviceControlInputRouter router(&window);
    router.setConnected(true);
    router.setKeyboardModeSelected(true);
    router.setKeyboardArmed(true);

    QDialog dialog(&window);
    dialog.setModal(true);
    dialog.show();
    dialog.activateWindow();
    dialog.setFocus();
    QTRY_VERIFY(!router.keyboardArmed());
    dialog.close();

    router.setKeyboardArmed(true);
    QVERIFY(router.keyboardArmed());
    QEvent deactivate(QEvent::ApplicationDeactivate);
    QCoreApplication::sendEvent(qApp, &deactivate);
    QVERIFY(!router.keyboardArmed());
}

QTEST_MAIN(DeviceControlInputRouterTest)
#include "DeviceControlInputRouterTest.moc"
