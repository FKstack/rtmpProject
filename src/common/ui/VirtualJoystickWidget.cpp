#include "ui/VirtualJoystickWidget.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPropertyAnimation>

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kDeadZoneRatio = 0.20;
constexpr qreal kSectorHysteresisDegrees = 10.0;
constexpr int kReturnDurationMs = 120;
constexpr qreal kPi = 3.14159265358979323846;

qreal commandAngle(DeviceCommand command)
{
    switch (command) {
    case DeviceCommand::TurnRight: return 0.0;
    case DeviceCommand::MoveForward: return 90.0;
    case DeviceCommand::TurnLeft: return 180.0;
    case DeviceCommand::MoveBackward: return 270.0;
    default: return 0.0;
    }
}

qreal angularDistance(qreal lhs, qreal rhs)
{
    qreal difference = std::fmod(std::abs(lhs - rhs), 360.0);
    return difference > 180.0 ? 360.0 - difference : difference;
}

} // namespace

VirtualJoystickWidget::VirtualJoystickWidget(QWidget *parent)
    : QWidget(parent), returnAnimation_(new QPropertyAnimation(this, "knobOffset", this))
{
    setObjectName(QStringLiteral("virtualJoystick"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(tr("车辆虚拟摇杆"));
    setAccessibleDescription(
        tr("按住鼠标左键拖向前后左右，回到中心或松开时停车"));
    setToolTip(tr("按住并拖动控制方向；松开鼠标立即停车"));
    returnAnimation_->setDuration(kReturnDurationMs);
    returnAnimation_->setEasingCurve(QEasingCurve::OutCubic);
}

QSize VirtualJoystickWidget::sizeHint() const { return {196, 196}; }

QSize VirtualJoystickWidget::minimumSizeHint() const { return {168, 168}; }

bool VirtualJoystickWidget::isDriving() const noexcept
{
    return currentCommand_.has_value();
}

QPointF VirtualJoystickWidget::knobOffset() const noexcept { return knobOffset_; }

void VirtualJoystickWidget::setControlEnabled(bool enabled)
{
    if (controlEnabled_ == enabled) {
        setEnabled(enabled);
        return;
    }
    if (!enabled) finishInteraction(true);
    controlEnabled_ = enabled;
    setEnabled(enabled);
    update();
}

void VirtualJoystickWidget::cancelMovement() { finishInteraction(true); }

QPointF VirtualJoystickWidget::centerPoint() const
{
    return QPointF(rect().center());
}

qreal VirtualJoystickWidget::baseRadius() const
{
    return std::max<qreal>(0.0, std::min(width(), height()) * 0.5 - 10.0);
}

qreal VirtualJoystickWidget::maximumOffset() const
{
    return std::max<qreal>(0.0, baseRadius() * 0.66);
}

QPointF VirtualJoystickWidget::clampedOffset(const QPointF &position) const
{
    QPointF offset = position - centerPoint();
    const qreal length = std::hypot(offset.x(), offset.y());
    const qreal limit = maximumOffset();
    if (length > limit && length > 0.0) offset *= limit / length;
    return offset;
}

std::optional<DeviceCommand> VirtualJoystickWidget::commandForOffset(
    const QPointF &offset) const
{
    const qreal length = std::hypot(offset.x(), offset.y());
    if (length < maximumOffset() * kDeadZoneRatio) return std::nullopt;

    qreal angle = std::atan2(-offset.y(), offset.x()) * 180.0 / kPi;
    if (angle < 0.0) angle += 360.0;
    if (currentCommand_.has_value() &&
        angularDistance(angle, commandAngle(*currentCommand_)) <=
            45.0 + kSectorHysteresisDegrees) {
        return currentCommand_;
    }

    if (angle >= 45.0 && angle < 135.0)
        return DeviceCommand::MoveForward;
    if (angle >= 135.0 && angle < 225.0)
        return DeviceCommand::TurnLeft;
    if (angle >= 225.0 && angle < 315.0)
        return DeviceCommand::MoveBackward;
    return DeviceCommand::TurnRight;
}

void VirtualJoystickWidget::updatePointer(const QPointF &position)
{
    setKnobOffset(clampedOffset(position));
    const std::optional<DeviceCommand> next = commandForOffset(knobOffset_);
    if (next == currentCommand_) return;
    const bool wasDriving = currentCommand_.has_value();
    currentCommand_ = next;
    if (currentCommand_.has_value()) {
        emit commandPressed(*currentCommand_);
    } else if (wasDriving) {
        emit movementReleased();
    }
    update();
}

void VirtualJoystickWidget::finishInteraction(bool animate)
{
    const bool wasDriving = currentCommand_.has_value();
    currentCommand_.reset();
    if (pointerCaptured_) {
        pointerCaptured_ = false;
        if (mouseGrabber() == this) releaseMouse();
    }
    if (wasDriving) emit movementReleased();

    returnAnimation_->stop();
    if (animate && !knobOffset_.isNull()) {
        returnAnimation_->setStartValue(knobOffset_);
        returnAnimation_->setEndValue(QPointF{});
        returnAnimation_->start();
    } else {
        setKnobOffset({});
    }
    update();
}

void VirtualJoystickWidget::setKnobOffset(const QPointF &offset)
{
    knobOffset_ = offset;
    update();
}

void VirtualJoystickWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPalette colors = palette();
    const QPalette::ColorGroup group = isEnabled()
        ? QPalette::Active
        : QPalette::Disabled;
    const QColor accent = colors.color(group, QPalette::Highlight);
    const QColor foreground = colors.color(group, QPalette::Text);
    const QColor surface = colors.color(group, QPalette::Base);
    const QColor raisedSurface = colors.color(group, QPalette::Button);
    const QColor border = colors.color(group, QPalette::Midlight);

    const QPointF center = centerPoint();
    const qreal radius = baseRadius();
    const QRectF base(center.x() - radius, center.y() - radius,
                      radius * 2.0, radius * 2.0);

    painter.setPen(QPen(border, 1.5));
    painter.setBrush(surface);
    painter.drawEllipse(base);

    if (currentCommand_.has_value()) {
        const qreal qtStart = commandAngle(*currentCommand_) - 45.0;
        painter.setPen(Qt::NoPen);
        QColor directionHighlight = accent;
        directionHighlight.setAlpha(64);
        painter.setBrush(directionHighlight);
        QPainterPath wedge;
        wedge.moveTo(center);
        wedge.arcTo(base, qtStart, 90.0);
        wedge.closeSubpath();
        painter.drawPath(wedge);
    }

    painter.setPen(QPen(colors.color(group, QPalette::Mid), 1.0, Qt::DashLine));
    const qreal deadRadius = maximumOffset() * kDeadZoneRatio;
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, deadRadius, deadRadius);

    painter.setPen(foreground);
    QFont directionFont = font();
    directionFont.setBold(true);
    directionFont.setPointSizeF(std::max(8.0, directionFont.pointSizeF()));
    painter.setFont(directionFont);
    painter.drawText(QRectF(center.x() - 14, base.top() + 8, 28, 22),
                     Qt::AlignCenter, QStringLiteral("W"));
    painter.drawText(QRectF(center.x() - 14, base.bottom() - 30, 28, 22),
                     Qt::AlignCenter, QStringLiteral("S"));
    painter.drawText(QRectF(base.left() + 8, center.y() - 11, 28, 22),
                     Qt::AlignCenter, QStringLiteral("A"));
    painter.drawText(QRectF(base.right() - 36, center.y() - 11, 28, 22),
                     Qt::AlignCenter, QStringLiteral("D"));

    const QPointF knobCenter = center + knobOffset_;
    const qreal knobRadius = std::max<qreal>(19.0, radius * 0.24);
    painter.setPen(QPen(accent.lighter(135), 1.5));
    painter.setBrush(isEnabled() ? accent.darker(108) : raisedSurface);
    painter.drawEllipse(knobCenter, knobRadius, knobRadius);
    painter.setPen(colors.color(group, QPalette::HighlightedText));
    painter.drawText(QRectF(knobCenter.x() - knobRadius,
                            knobCenter.y() - knobRadius,
                            knobRadius * 2.0, knobRadius * 2.0),
                     Qt::AlignCenter, QStringLiteral("●"));

    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(accent.lighter(135), 2.0, Qt::DashLine));
        painter.drawEllipse(base.adjusted(2.0, 2.0, -2.0, -2.0));
    }
}

void VirtualJoystickWidget::mousePressEvent(QMouseEvent *event)
{
    if (!controlEnabled_ || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (std::hypot((event->position() - centerPoint()).x(),
                   (event->position() - centerPoint()).y()) > baseRadius()) {
        event->ignore();
        return;
    }
    returnAnimation_->stop();
    pointerCaptured_ = true;
    setFocus(Qt::MouseFocusReason);
    grabMouse();
    updatePointer(event->position());
    event->accept();
}

void VirtualJoystickWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!pointerCaptured_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    updatePointer(event->position());
    event->accept();
}

void VirtualJoystickWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!pointerCaptured_ || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    finishInteraction(true);
    event->accept();
}

bool VirtualJoystickWidget::event(QEvent *event)
{
    if (event != nullptr &&
        (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide ||
         event->type() == QEvent::WindowDeactivate)) {
        finishInteraction(true);
    }
    return QWidget::event(event);
}
