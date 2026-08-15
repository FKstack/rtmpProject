#include "ui/FullscreenChromeController.h"

#include <algorithm>

#include <QAbstractAnimation>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

#include "ui/FullscreenControlBar.h"

FullscreenChromeController::FullscreenChromeController(
    QWidget *window,
    QWidget *revealZone,
    FullscreenControlBar *controlBar,
    QObject *parent
)
    : QObject(parent),
      window_(window),
      revealZone_(revealZone),
      controlBar_(controlBar)
{
    animation_ = new QPropertyAnimation(controlBar_, "geometry", this);
    animation_->setObjectName(QStringLiteral("fullscreenControlBarAnimation"));
    animation_->setDuration(kAnimationDurationMs);
    animation_->setEasingCurve(QEasingCurve::OutCubic);

    hideTimer_ = new QTimer(this);
    hideTimer_->setSingleShot(true);
    cursorTimer_ = new QTimer(this);
    cursorTimer_->setObjectName(QStringLiteral("fullscreenCursorHideTimer"));
    cursorTimer_->setSingleShot(true);

    connect(hideTimer_, &QTimer::timeout, this, [this] { hide(true); });
    connect(animation_, &QPropertyAnimation::finished, this, [this] {
        if (!targetVisible_) {
            controlBar_->hide();
            scheduleCursorHide();
        }
    });
    connect(cursorTimer_, &QTimer::timeout, this, [this] {
        if (fullscreen_ && frameVisible_ && !controlBar_->isVisible() &&
            !pointerInRevealZone_ && !pointerInControlBar_) {
            window_->setCursor(Qt::BlankCursor);
        }
    });
}

void FullscreenChromeController::setPresentationState(
    bool fullscreen,
    bool frameVisible
)
{
    fullscreen_ = fullscreen;
    frameVisible_ = frameVisible;
}

void FullscreenChromeController::resetPointerState()
{
    pointerInRevealZone_ = false;
    pointerInControlBar_ = false;
}

void FullscreenChromeController::updatePointerPosition(const QPointF &position)
{
    pointerInRevealZone_ = isPointerInRevealArea(position);
    pointerInControlBar_ = isPointerOverControlBar(position);
}

void FullscreenChromeController::handlePointerActivity(const QPointF &position)
{
    if (!fullscreen_) {
        return;
    }
    window_->setCursor(Qt::ArrowCursor);
    cursorTimer_->stop();
    updatePointerPosition(position);
    if (pointerInRevealZone_ || pointerInControlBar_) {
        show(true);
        return;
    }
    scheduleHide();
    if (!controlBar_->isVisible()) {
        scheduleCursorHide();
    }
}

void FullscreenChromeController::pointerEnteredControlBar()
{
    pointerInControlBar_ = true;
    hideTimer_->stop();
    cursorTimer_->stop();
    window_->setCursor(Qt::ArrowCursor);
}

void FullscreenChromeController::pointerLeftControlBar()
{
    pointerInControlBar_ = false;
    scheduleHide();
}

void FullscreenChromeController::show(bool animated)
{
    if (!fullscreen_) {
        return;
    }
    hideTimer_->stop();
    cursorTimer_->stop();
    window_->setCursor(Qt::ArrowCursor);
    const QRect target = visibleGeometry();
    if (targetVisible_ && controlBar_->isVisible() &&
        (animation_->state() == QAbstractAnimation::Running ||
         controlBar_->geometry() == target)) {
        revealZone_->raise();
        controlBar_->raise();
        return;
    }
    targetVisible_ = true;
    animation_->stop();
    if (!controlBar_->isVisible()) {
        controlBar_->setGeometry(animated ? hiddenGeometry() : target);
        controlBar_->show();
    }
    revealZone_->raise();
    controlBar_->raise();
    if (!animated || controlBar_->geometry() == target) {
        controlBar_->setGeometry(target);
        return;
    }
    animation_->setStartValue(controlBar_->geometry());
    animation_->setEndValue(target);
    animation_->start();
}

void FullscreenChromeController::scheduleHide(int delayMs)
{
    if (!fullscreen_ || !frameVisible_) {
        show(false);
        return;
    }
    if (!pointerInRevealZone_ && !pointerInControlBar_ && !hideTimer_->isActive()) {
        hideTimer_->start(std::max(0, delayMs));
    }
}

void FullscreenChromeController::hide(bool animated)
{
    if (!fullscreen_ || !frameVisible_ || pointerInRevealZone_ ||
        pointerInControlBar_) {
        return;
    }
    if (!targetVisible_ &&
        (animation_->state() == QAbstractAnimation::Running ||
         !controlBar_->isVisible())) {
        return;
    }
    targetVisible_ = false;
    animation_->stop();
    if (!controlBar_->isVisible()) {
        scheduleCursorHide();
        return;
    }
    if (!animated) {
        controlBar_->setGeometry(hiddenGeometry());
        controlBar_->hide();
        scheduleCursorHide();
        return;
    }
    animation_->setStartValue(controlBar_->geometry());
    animation_->setEndValue(hiddenGeometry());
    animation_->start();
}

void FullscreenChromeController::position()
{
    controlBar_->adjustSize();
    animation_->stop();
    controlBar_->setGeometry(targetVisible_ ? visibleGeometry() : hiddenGeometry());
}

void FullscreenChromeController::stopMotion()
{
    hideTimer_->stop();
    cursorTimer_->stop();
    animation_->stop();
    targetVisible_ = false;
    controlBar_->hide();
}

void FullscreenChromeController::updateGeometry()
{
    revealZone_->setGeometry(
        0, qMax(0, window_->height() - kRevealHeight),
        window_->width(), qMin(window_->height(), kRevealHeight)
    );
    position();
    revealZone_->raise();
    if (controlBar_->isVisible()) {
        controlBar_->raise();
    }
}

bool FullscreenChromeController::pointerInRevealZone() const noexcept
{
    return pointerInRevealZone_;
}

bool FullscreenChromeController::pointerInControlBar() const noexcept
{
    return pointerInControlBar_;
}

void FullscreenChromeController::scheduleCursorHide()
{
    if (!fullscreen_ || !frameVisible_ || controlBar_->isVisible() ||
        pointerInRevealZone_ || pointerInControlBar_) {
        cursorTimer_->stop();
        return;
    }
    cursorTimer_->start(kCursorHideDelayMs);
}

bool FullscreenChromeController::isPointerInRevealArea(
    const QPointF &position
) const noexcept
{
    return position.y() >= window_->height() - kRevealHeight;
}

bool FullscreenChromeController::isPointerOverControlBar(
    const QPointF &position
) const noexcept
{
    if (!controlBar_->isVisible()) {
        return false;
    }
    const QPoint topLeft = controlBar_->mapTo(window_, QPoint(0, 0));
    return QRect(topLeft, controlBar_->size()).contains(position.toPoint());
}

QRect FullscreenChromeController::visibleGeometry() const
{
    const QSize size = controlBar_->sizeHint();
    const int x = (revealZone_->width() - size.width()) / 2;
    const int y = revealZone_->height() - size.height() - kBottomMargin;
    return QRect(x, qMax(0, y), size.width(), size.height());
}

QRect FullscreenChromeController::hiddenGeometry() const
{
    QRect geometry = visibleGeometry();
    geometry.moveTop(revealZone_->height() + 1);
    return geometry;
}
