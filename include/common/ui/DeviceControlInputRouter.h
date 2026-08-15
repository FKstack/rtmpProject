#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

#include <optional>

#include "device_control/DeviceControlTypes.h"

class QWidget;

/**
 * @brief Routes explicitly armed desktop keyboard input to device commands.
 *
 * The router owns only session-local key and arming state. It is installed on
 * QApplication so keyboard control survives normal focus movement within the
 * main window, while text editors, modal dialogs and inactive windows remain
 * outside its scope.
 */
class DeviceControlInputRouter final : public QObject
{
    Q_OBJECT

public:
    explicit DeviceControlInputRouter(QWidget *scopeWindow,
                                      QObject *parent = nullptr);
    ~DeviceControlInputRouter() override;

    [[nodiscard]] bool keyboardArmed() const noexcept;

public slots:
    void setConnected(bool connected);
    void setKeyboardModeSelected(bool selected);
    void setKeyboardArmed(bool armed);
    void clearMovementState();
    void cancelAndDisarm();

signals:
    void commandPressed(DeviceCommand command);
    void movementReleased();
    void keyboardArmedChanged(bool armed);
    void directionKeyStateChanged(DeviceCommand command, bool pressed);
    void explicitLockRequested();
    void controlContextLost();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] static std::optional<DeviceCommand> commandForKey(int key);
    [[nodiscard]] static bool isTextEntryWidget(const QWidget *widget);
    [[nodiscard]] std::optional<DeviceCommand> effectiveCommand() const;
    [[nodiscard]] bool anyHeldFor(DeviceCommand command) const;
    void handleKeyPress(int key);
    void handleKeyRelease(int key);
    void clearHeldKeys(bool requestStop);
    void emitVisualChanges(const QList<int> &before);

    QPointer<QWidget> scopeWindow_;
    QList<int> heldKeys_;
    std::optional<DeviceCommand> currentCommand_;
    bool connected_ = false;
    bool keyboardModeSelected_ = false;
    bool keyboardArmed_ = false;
};
