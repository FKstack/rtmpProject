#pragma once

#include <QWidget>

#include "device_control/DeviceControlTypes.h"

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QToolButton;
class VirtualJoystickWidget;

/** @brief Desktop view for MQTT status, stream commands and vehicle input. */
class DeviceControlPanel final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kMaximumObservedMessages = 20;

    explicit DeviceControlPanel(QWidget *parent = nullptr);
    [[nodiscard]] QSize sizeHint() const override;
    void setConnectionState(MqttConnectionState state, const QString &detail = {});
    void setTopic(const QString &topic);
    void setTopics(const QString &controlTopic, const QString &statusTopic);
    void setControlTarget(const QString &deviceId, DevicePresenceState state);
    void setDevicePresenceState(DevicePresenceState state);
    void setLastResult(const QString &text, bool error = false);
    void appendObservedMessage(const MqttObservedMessage &message);
    void showObservedMessagesDropped(quint64 count);
    void setKeyboardArmedState(bool armed);
    void setKeyboardDirectionState(DeviceCommand command, bool pressed);

public slots:
    void cancelInteractiveControl();

signals:
    void commandPressed(DeviceCommand command);
    void movementReleased();
    void settingsRequested();
    void keyboardModeSelected(bool selected);
    void keyboardArmRequested(bool armed);
    void inputResetRequested();
    void controlContextLost();

protected:
    bool event(QEvent *event) override;

private:
    void selectKeyboardMode(bool keyboard);
    void updateCommandEnabled();
    [[nodiscard]] QPushButton *keyButton(DeviceCommand command) const;

    QLabel *statusDot_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *statusDetailLabel_ = nullptr;
    QLabel *topicLabel_ = nullptr;
    QLabel *targetLabel_ = nullptr;
    QLabel *targetStateLabel_ = nullptr;
    QLabel *resultLabel_ = nullptr;
    QLabel *keyboardStatusLabel_ = nullptr;
    QListWidget *observedMessages_ = nullptr;
    QToolButton *observedToggle_ = nullptr;
    QStackedWidget *inputStack_ = nullptr;
    QPushButton *mouseModeButton_ = nullptr;
    QPushButton *keyboardModeButton_ = nullptr;
    QPushButton *keyboardArmButton_ = nullptr;
    QPushButton *forwardKey_ = nullptr;
    QPushButton *backwardKey_ = nullptr;
    QPushButton *leftKey_ = nullptr;
    QPushButton *rightKey_ = nullptr;
    QPushButton *startStreamButton_ = nullptr;
    QPushButton *stopStreamButton_ = nullptr;
    QPushButton *stopCarButton_ = nullptr;
    VirtualJoystickWidget *joystick_ = nullptr;
    bool connected_ = false;
    bool hasTarget_ = false;
    bool targetOnline_ = false;
    bool keyboardMode_ = false;
};
