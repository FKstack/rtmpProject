#include "ui/GridTransitionAnimator.h"

#include <utility>

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QWidget>

namespace {
constexpr int kDurationMs = 220;
}

GridTransitionAnimator::GridTransitionAnimator(QObject *parent)
    : QObject(parent)
{
}

bool GridTransitionAnimator::isRunning() const noexcept
{
    return animation_ != nullptr;
}

bool GridTransitionAnimator::start(
    const QVector<GridTransition> &transitions,
    QWidget *fadeInOverlay,
    std::function<void()> finished
)
{
    if (isRunning() || transitions.isEmpty()) {
        return false;
    }
    auto *group = new QParallelAnimationGroup(this);
    animation_ = group;
    for (const GridTransition &transition : transitions) {
        if (transition.overlay == nullptr) {
            continue;
        }
        auto *animation = new QPropertyAnimation(
            transition.overlay, "geometry", group
        );
        animation->setDuration(kDurationMs);
        animation->setStartValue(transition.startGeometry);
        animation->setEndValue(transition.endGeometry);
        animation->setEasingCurve(QEasingCurve::OutCubic);
    }
    if (fadeInOverlay != nullptr) {
        auto *effect = new QGraphicsOpacityEffect(fadeInOverlay);
        effect->setOpacity(0.0);
        fadeInOverlay->setGraphicsEffect(effect);
        auto *opacity = new QPropertyAnimation(effect, "opacity", group);
        opacity->setDuration(kDurationMs);
        opacity->setStartValue(0.0);
        opacity->setEndValue(1.0);
        opacity->setEasingCurve(QEasingCurve::OutCubic);
    }
    connect(group, &QParallelAnimationGroup::finished, this,
            [this, group, finished = std::move(finished)] {
                if (animation_ != group) {
                    return;
                }
                animation_ = nullptr;
                if (finished) {
                    finished();
                }
            });
    group->start(QAbstractAnimation::DeleteWhenStopped);
    return true;
}
