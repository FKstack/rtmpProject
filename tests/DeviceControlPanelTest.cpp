#include <QtTest>

#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QToolButton>

#include "ui/DeviceControlPanel.h"
#include "ui/VirtualJoystickWidget.h"

class DeviceControlPanelTest final : public QObject
{
    Q_OBJECT
private slots:
    void disablesCommandsUntilConnected();
    void switchesModesWithoutArmingKeyboard();
    void keyboardVisualStateIsExplicit();
    void observedMessagesAreCollapsedBoundedAndEscaped();
    void hidingPanelInvalidatesControlContext();
};

void DeviceControlPanelTest::disablesCommandsUntilConnected()
{
    DeviceControlPanel panel;
    QVERIFY(panel.minimumWidth() >= 320);
    QVERIFY(panel.sizeHint().width() >= 360);
    auto *start = panel.findChild<QPushButton *>(
        QStringLiteral("startDeviceStreamButton"));
    auto *stop = panel.findChild<QPushButton *>(
        QStringLiteral("stopDeviceStreamButton"));
    auto *stopCar = panel.findChild<QPushButton *>(
        QStringLiteral("stopCarButton"));
    auto *joystick = panel.findChild<VirtualJoystickWidget *>();
    QVERIFY(start != nullptr);
    QVERIFY(stop != nullptr);
    QVERIFY(stopCar != nullptr);
    QVERIFY(joystick != nullptr);
    QVERIFY(!start->isEnabled());
    QVERIFY(!stop->isEnabled());
    QVERIFY(!stopCar->isEnabled());
    QVERIFY(!joystick->isEnabled());

    panel.setConnectionState(MqttConnectionState::Connected);
    QVERIFY(start->isEnabled());
    QVERIFY(stop->isEnabled());
    QVERIFY(stopCar->isEnabled());
    QVERIFY(joystick->isEnabled());
}

void DeviceControlPanelTest::switchesModesWithoutArmingKeyboard()
{
    DeviceControlPanel panel;
    panel.setConnectionState(MqttConnectionState::Connected);
    QSignalSpy modeSpy(&panel, &DeviceControlPanel::keyboardModeSelected);
    QSignalSpy armSpy(&panel, &DeviceControlPanel::keyboardArmRequested);
    auto *keyboardMode = panel.findChild<QPushButton *>(
        QStringLiteral("keyboardControlModeButton"));
    auto *arm = panel.findChild<QPushButton *>(
        QStringLiteral("keyboardArmButton"));
    auto *stack = panel.findChild<QStackedWidget *>(
        QStringLiteral("deviceControlInputStack"));
    QVERIFY(keyboardMode != nullptr);
    QVERIFY(arm != nullptr);
    QVERIFY(stack != nullptr);

    keyboardMode->click();
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(stack->currentIndex(), 1);
    QVERIFY(arm->isEnabled());
    QVERIFY(!arm->isChecked());

    arm->click();
    QCOMPARE(armSpy.count(), 1);
    QCOMPARE(armSpy.takeFirst().at(0).toBool(), true);
}

void DeviceControlPanelTest::keyboardVisualStateIsExplicit()
{
    DeviceControlPanel panel;
    auto *arm = panel.findChild<QPushButton *>(
        QStringLiteral("keyboardArmButton"));
    QVERIFY(arm != nullptr);
    panel.setKeyboardArmedState(true);
    QVERIFY(arm->isChecked());
    QCOMPARE(arm->text(), QStringLiteral("解除键盘控制"));

    panel.setKeyboardDirectionState(DeviceCommand::MoveForward, true);
    bool foundPressedKey = false;
    for (QPushButton *button : panel.findChildren<QPushButton *>()) {
        if (button->accessibleName() == QStringLiteral("前进键")) {
            foundPressedKey = button->property("pressed").toBool();
        }
    }
    QVERIFY(foundPressedKey);
}

void DeviceControlPanelTest::observedMessagesAreCollapsedBoundedAndEscaped()
{
    DeviceControlPanel panel;
    auto *messages = panel.findChild<QListWidget *>(
        QStringLiteral("mqttObservedMessages"));
    auto *toggle = panel.findChild<QToolButton *>(
        QStringLiteral("mqttObservedMessagesToggle"));
    QVERIFY(messages != nullptr);
    QVERIFY(toggle != nullptr);
    QVERIFY(messages->isHidden());
    toggle->setChecked(true);
    QVERIFY(!messages->isHidden());

    for (int index = 0; index < 25; ++index) {
        MqttObservedMessage message;
        message.topic = QStringLiteral("device/control");
        message.payload = QByteArray("<b>unsafe</b>\n") + QByteArray::number(index);
        message.receivedAtMs = 1780413730000LL + index;
        message.originalPayloadSize = message.payload.size() + (index == 24 ? 7 : 0);
        panel.appendObservedMessage(message);
    }
    QCOMPARE(messages->count(), DeviceControlPanel::kMaximumObservedMessages);
    const QString last = messages->item(messages->count() - 1)->text();
    QVERIFY(last.contains(QStringLiteral("来源未知")));
    QVERIFY(last.contains(QStringLiteral("<b>unsafe</b>\\n24")));
    QVERIFY(last.contains(QStringLiteral("已截断")));
}

void DeviceControlPanelTest::hidingPanelInvalidatesControlContext()
{
    DeviceControlPanel panel;
    QSignalSpy contextSpy(&panel, &DeviceControlPanel::controlContextLost);
    panel.show();
    QTest::qWait(10);
    panel.hide();
    QTRY_VERIFY(contextSpy.count() >= 1);
}

QTEST_MAIN(DeviceControlPanelTest)
#include "DeviceControlPanelTest.moc"
