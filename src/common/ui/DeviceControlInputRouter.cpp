#include "ui/DeviceControlInputRouter.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

#include <array>

namespace {

constexpr std::array<DeviceCommand, 4> kMovementCommands = {
    DeviceCommand::MoveForward,
    DeviceCommand::MoveBackward,
    DeviceCommand::TurnLeft,
    DeviceCommand::TurnRight
};

} // namespace

DeviceControlInputRouter::DeviceControlInputRouter(QWidget *scopeWindow,
                                                   QObject *parent)
    : QObject(parent), scopeWindow_(scopeWindow)
{
    Q_ASSERT(scopeWindow_ != nullptr);
    if (qApp != nullptr) qApp->installEventFilter(this);
}

DeviceControlInputRouter::~DeviceControlInputRouter()
{
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

bool DeviceControlInputRouter::keyboardArmed() const noexcept
{
    return keyboardArmed_;
}

void DeviceControlInputRouter::setConnected(bool connected)
{
    connected_ = connected;
    if (!connected_) cancelAndDisarm();
}

void DeviceControlInputRouter::setKeyboardModeSelected(bool selected)
{
    if (keyboardModeSelected_ == selected) return;
    keyboardModeSelected_ = selected;
    if (!selected) clearHeldKeys(true);
}

void DeviceControlInputRouter::setKeyboardArmed(bool armed)
{
    if (!armed || !connected_ || scopeWindow_ == nullptr) {
        cancelAndDisarm();
        return;
    }
    if (keyboardArmed_) return;
    keyboardArmed_ = true;
    emit keyboardArmedChanged(true);
}

void DeviceControlInputRouter::clearMovementState()
{
    clearHeldKeys(false);
}

void DeviceControlInputRouter::cancelAndDisarm()
{
    clearHeldKeys(true);
    if (!keyboardArmed_) return;
    keyboardArmed_ = false;
    emit keyboardArmedChanged(false);
}

std::optional<DeviceCommand> DeviceControlInputRouter::commandForKey(int key)
{
    switch (key) {
    case Qt::Key_W:
    case Qt::Key_Up: return DeviceCommand::MoveForward;
    case Qt::Key_S:
    case Qt::Key_Down: return DeviceCommand::MoveBackward;
    case Qt::Key_A:
    case Qt::Key_Left: return DeviceCommand::TurnLeft;
    case Qt::Key_D:
    case Qt::Key_Right: return DeviceCommand::TurnRight;
    default: return std::nullopt;
    }
}

bool DeviceControlInputRouter::isTextEntryWidget(const QWidget *widget)
{
    if (widget == nullptr) return false;
    if (qobject_cast<const QLineEdit *>(widget) != nullptr ||
        qobject_cast<const QTextEdit *>(widget) != nullptr ||
        qobject_cast<const QPlainTextEdit *>(widget) != nullptr ||
        qobject_cast<const QAbstractSpinBox *>(widget) != nullptr) {
        return true;
    }
    const auto *combo = qobject_cast<const QComboBox *>(widget);
    return combo != nullptr && combo->isEditable();
}

std::optional<DeviceCommand> DeviceControlInputRouter::effectiveCommand() const
{
    for (auto iterator = heldKeys_.crbegin(); iterator != heldKeys_.crend(); ++iterator) {
        const auto command = commandForKey(*iterator);
        if (command.has_value()) return command;
    }
    return std::nullopt;
}

bool DeviceControlInputRouter::anyHeldFor(DeviceCommand command) const
{
    for (int key : heldKeys_) {
        if (commandForKey(key) == command) return true;
    }
    return false;
}

void DeviceControlInputRouter::handleKeyPress(int key)
{
    if (heldKeys_.contains(key)) return;
    const QList<int> before = heldKeys_;
    heldKeys_.append(key);
    emitVisualChanges(before);
    const auto next = effectiveCommand();
    if (next == currentCommand_) return;
    currentCommand_ = next;
    if (currentCommand_.has_value()) emit commandPressed(*currentCommand_);
}

void DeviceControlInputRouter::handleKeyRelease(int key)
{
    if (!heldKeys_.contains(key)) return;
    const QList<int> before = heldKeys_;
    heldKeys_.removeAll(key);
    emitVisualChanges(before);
    const auto next = effectiveCommand();
    if (next == currentCommand_) return;
    const bool wasMoving = currentCommand_.has_value();
    currentCommand_ = next;
    if (currentCommand_.has_value()) {
        emit commandPressed(*currentCommand_);
    } else if (wasMoving) {
        emit movementReleased();
    }
}

void DeviceControlInputRouter::clearHeldKeys(bool requestStop)
{
    const QList<int> before = heldKeys_;
    const bool wasMoving = currentCommand_.has_value();
    heldKeys_.clear();
    currentCommand_.reset();
    emitVisualChanges(before);
    if (requestStop && wasMoving) emit movementReleased();
}

void DeviceControlInputRouter::emitVisualChanges(const QList<int> &before)
{
    for (DeviceCommand command : kMovementCommands) {
        bool wasPressed = false;
        for (int key : before) {
            if (commandForKey(key) == command) {
                wasPressed = true;
                break;
            }
        }
        const bool pressed = anyHeldFor(command);
        if (pressed != wasPressed) emit directionKeyStateChanged(command, pressed);
    }
}

bool DeviceControlInputRouter::eventFilter(QObject *watched, QEvent *event)
{
    if (event == nullptr) return QObject::eventFilter(watched, event);

    if (keyboardArmed_ &&
        (event->type() == QEvent::ApplicationDeactivate ||
         (watched == scopeWindow_ &&
          (event->type() == QEvent::WindowDeactivate ||
           event->type() == QEvent::Hide)))) {
        emit controlContextLost();
        cancelAndDisarm();
    }

    if (keyboardArmed_ && event->type() == QEvent::FocusIn) {
        if (QApplication::activeModalWidget() != nullptr) {
            emit controlContextLost();
            cancelAndDisarm();
        } else if (isTextEntryWidget(qobject_cast<QWidget *>(watched))) {
            clearHeldKeys(true);
        }
    }

    if (!keyboardArmed_ ||
        !keyboardModeSelected_ ||
        (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)) {
        return QObject::eventFilter(watched, event);
    }

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const bool pressed = event->type() == QEvent::KeyPress;
    const int key = keyEvent->key();
    const auto movement = commandForKey(key);

    const auto *targetWidget = qobject_cast<QWidget *>(watched);
    if (targetWidget != nullptr && targetWidget->window() != scopeWindow_) {
        emit controlContextLost();
        cancelAndDisarm();
        return QObject::eventFilter(watched, event);
    }

    if (pressed && (keyEvent->modifiers() &
                    (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return QObject::eventFilter(watched, event);
    }

    const bool textEntry = isTextEntryWidget(QApplication::focusWidget());
    const bool modal = QApplication::activeModalWidget() != nullptr;
    if (textEntry || modal) return QObject::eventFilter(watched, event);

    if (key == Qt::Key_Escape) {
        if (pressed && !keyEvent->isAutoRepeat()) {
            emit explicitLockRequested();
            cancelAndDisarm();
        }
        return true;
    }
    if (key == Qt::Key_Space) {
        if (pressed && !keyEvent->isAutoRepeat()) {
            clearHeldKeys(false);
            emit commandPressed(DeviceCommand::StopCar);
        }
        return true;
    }
    if (!movement.has_value()) return QObject::eventFilter(watched, event);
    if (keyEvent->isAutoRepeat()) return true;

    if (pressed) handleKeyPress(key);
    else handleKeyRelease(key);
    return true;
}
