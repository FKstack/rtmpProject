#pragma once

#include <functional>

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QVector>

class QParallelAnimationGroup;
class QWidget;

struct GridTransition
{
    QPointer<QWidget> overlay;
    QRect startGeometry;
    QRect endGeometry;
};

/** @brief Owns the lifetime and completion contract of grid animations. */
class GridTransitionAnimator final : public QObject
{
    Q_OBJECT

public:
    explicit GridTransitionAnimator(QObject *parent = nullptr);
    [[nodiscard]] bool isRunning() const noexcept;
    bool start(const QVector<GridTransition> &transitions,
               QWidget *fadeInOverlay, std::function<void()> finished);

private:
    QPointer<QParallelAnimationGroup> animation_;
};
