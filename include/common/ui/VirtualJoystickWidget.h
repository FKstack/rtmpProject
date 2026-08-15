#pragma once

#include <QPointF>
#include <QWidget>

#include <optional>

#include "device_control/DeviceControlTypes.h"

class QMouseEvent;
class QPaintEvent;
class QPropertyAnimation;

/**
 * @brief Desktop mouse joystick that converts pointer motion to four commands.
 *
 * The widget owns pointer geometry and visual feedback only. It never talks to
 * MQTT and it deliberately exposes the existing four-direction wire contract.
 * All methods and signals belong to the Qt UI thread.
 */
class VirtualJoystickWidget final : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QPointF knobOffset READ knobOffset WRITE setKnobOffset)

public:
    explicit VirtualJoystickWidget(QWidget *parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] bool isDriving() const noexcept;
    [[nodiscard]] QPointF knobOffset() const noexcept;

public slots:
    void setControlEnabled(bool enabled);
    void cancelMovement();

signals:
    void commandPressed(DeviceCommand command);
    void movementReleased();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    [[nodiscard]] QPointF centerPoint() const;
    [[nodiscard]] qreal baseRadius() const;
    [[nodiscard]] qreal maximumOffset() const;
    [[nodiscard]] QPointF clampedOffset(const QPointF &position) const;
    [[nodiscard]] std::optional<DeviceCommand> commandForOffset(
        const QPointF &offset) const;
    void updatePointer(const QPointF &position);
    void finishInteraction(bool animate);
    void setKnobOffset(const QPointF &offset);

    QPointF knobOffset_;
    std::optional<DeviceCommand> currentCommand_;
    QPropertyAnimation *returnAnimation_ = nullptr;
    bool controlEnabled_ = false;
    bool pointerCaptured_ = false;
};
