#include <QtTest>

#include <QSignalSpy>

#include "ui/VirtualJoystickWidget.h"

class VirtualJoystickWidgetTest final : public QObject
{
    Q_OBJECT
private slots:
    void centerDeadZoneDoesNotMove();
    void cardinalDirections_data();
    void cardinalDirections();
    void sameDirectionAndBoundaryJitterDoNotSpam();
    void releaseStopsBeforeReturnAnimationCompletes();
    void losingMouseCaptureStopsOnce();
    void hidingWhileDrivingStopsOnce();
};

void VirtualJoystickWidgetTest::centerDeadZoneDoesNotMove()
{
    VirtualJoystickWidget joystick;
    joystick.resize(200, 200);
    joystick.setControlEnabled(true);
    joystick.show();
    QSignalSpy commandSpy(&joystick, &VirtualJoystickWidget::commandPressed);
    QSignalSpy stopSpy(&joystick, &VirtualJoystickWidget::movementReleased);
    QTest::mousePress(&joystick, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseRelease(&joystick, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QCOMPARE(commandSpy.count(), 0);
    QCOMPARE(stopSpy.count(), 0);
}

void VirtualJoystickWidgetTest::cardinalDirections_data()
{
    QTest::addColumn<QPoint>("position");
    QTest::addColumn<DeviceCommand>("command");
    QTest::newRow("forward") << QPoint(100, 28) << DeviceCommand::MoveForward;
    QTest::newRow("backward") << QPoint(100, 172) << DeviceCommand::MoveBackward;
    QTest::newRow("left") << QPoint(28, 100) << DeviceCommand::TurnLeft;
    QTest::newRow("right") << QPoint(172, 100) << DeviceCommand::TurnRight;
}

void VirtualJoystickWidgetTest::cardinalDirections()
{
    QFETCH(QPoint, position);
    QFETCH(DeviceCommand, command);
    VirtualJoystickWidget joystick;
    joystick.resize(200, 200);
    joystick.setControlEnabled(true);
    joystick.show();
    QSignalSpy commandSpy(&joystick, &VirtualJoystickWidget::commandPressed);
    QSignalSpy stopSpy(&joystick, &VirtualJoystickWidget::movementReleased);
    QTest::mousePress(&joystick, Qt::LeftButton, Qt::NoModifier, position);
    QCOMPARE(commandSpy.count(), 1);
    QCOMPARE(qvariant_cast<DeviceCommand>(commandSpy.takeFirst().at(0)), command);
    QTest::mouseRelease(&joystick, Qt::LeftButton, Qt::NoModifier, position);
    QCOMPARE(stopSpy.count(), 1);
}

void VirtualJoystickWidgetTest::sameDirectionAndBoundaryJitterDoNotSpam()
{
    VirtualJoystickWidget joystick;
    joystick.resize(200, 200);
    joystick.setControlEnabled(true);
    joystick.show();
    QSignalSpy commandSpy(&joystick, &VirtualJoystickWidget::commandPressed);
    QTest::mousePress(&joystick, Qt::LeftButton, Qt::NoModifier, QPoint(170, 100));
    QTest::mouseMove(&joystick, QPoint(165, 96));
    QTest::mouseMove(&joystick, QPoint(150, 52));
    QCOMPARE(commandSpy.count(), 1);
    QCOMPARE(qvariant_cast<DeviceCommand>(commandSpy.at(0).at(0)),
             DeviceCommand::TurnRight);
    QTest::mouseMove(&joystick, QPoint(135, 35));
    QCOMPARE(commandSpy.count(), 2);
    QCOMPARE(qvariant_cast<DeviceCommand>(commandSpy.at(1).at(0)),
             DeviceCommand::MoveForward);
    QTest::mouseRelease(&joystick, Qt::LeftButton, Qt::NoModifier,
                        QPoint(135, 35));
}

void VirtualJoystickWidgetTest::releaseStopsBeforeReturnAnimationCompletes()
{
    VirtualJoystickWidget joystick;
    joystick.resize(200, 200);
    joystick.setControlEnabled(true);
    joystick.show();
    QSignalSpy stopSpy(&joystick, &VirtualJoystickWidget::movementReleased);
    QTest::mousePress(&joystick, Qt::LeftButton, Qt::NoModifier, QPoint(170, 100));
    QVERIFY(!joystick.knobOffset().isNull());
    QTest::mouseRelease(&joystick, Qt::LeftButton, Qt::NoModifier, QPoint(170, 100));
    QCOMPARE(stopSpy.count(), 1);
    QVERIFY(!joystick.knobOffset().isNull());
    QTRY_VERIFY_WITH_TIMEOUT(joystick.knobOffset().isNull(), 250);
}

void VirtualJoystickWidgetTest::losingMouseCaptureStopsOnce()
{
    VirtualJoystickWidget joystick;
    joystick.resize(200, 200);
    joystick.setControlEnabled(true);
    joystick.show();
    QSignalSpy stopSpy(&joystick, &VirtualJoystickWidget::movementReleased);
    QTest::mousePress(&joystick, Qt::LeftButton, Qt::NoModifier, QPoint(170, 100));
    QEvent ungrab(QEvent::UngrabMouse);
    QCoreApplication::sendEvent(&joystick, &ungrab);
    QCOMPARE(stopSpy.count(), 1);
    joystick.cancelMovement();
    QCOMPARE(stopSpy.count(), 1);
}

void VirtualJoystickWidgetTest::hidingWhileDrivingStopsOnce()
{
    VirtualJoystickWidget joystick;
    joystick.resize(200, 200);
    joystick.setControlEnabled(true);
    joystick.show();
    QSignalSpy stopSpy(&joystick, &VirtualJoystickWidget::movementReleased);
    QTest::mousePress(&joystick, Qt::LeftButton, Qt::NoModifier, QPoint(170, 100));
    joystick.hide();
    QTRY_COMPARE(stopSpy.count(), 1);
    joystick.cancelMovement();
    QCOMPARE(stopSpy.count(), 1);
}

QTEST_MAIN(VirtualJoystickWidgetTest)
#include "VirtualJoystickWidgetTest.moc"
