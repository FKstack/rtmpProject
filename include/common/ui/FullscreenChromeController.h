#pragma once

#include <QObject>
#include <QPointF>

class FullscreenControlBar;
class QPropertyAnimation;
class QTimer;
class QWidget;

/** @brief Owns fullscreen control-bar reveal, animation and cursor timers. */
class FullscreenChromeController final : public QObject
{
    Q_OBJECT

public:
    explicit FullscreenChromeController(QWidget *window, QWidget *revealZone,
                                        FullscreenControlBar *controlBar,
                                        QObject *parent = nullptr);

    void setPresentationState(bool fullscreen, bool frameVisible);
    void resetPointerState();
    void updatePointerPosition(const QPointF &position);
    void handlePointerActivity(const QPointF &position);
    void pointerEnteredControlBar();
    void pointerLeftControlBar();
    void show(bool animated);
    void scheduleHide(int delayMs = 250);
    void hide(bool animated = true);
    void position();
    void stopMotion();
    void updateGeometry();

    [[nodiscard]] bool pointerInRevealZone() const noexcept;
    [[nodiscard]] bool pointerInControlBar() const noexcept;

private:
    void scheduleCursorHide();
    [[nodiscard]] bool isPointerInRevealArea(const QPointF &position) const noexcept;
    [[nodiscard]] bool isPointerOverControlBar(const QPointF &position) const noexcept;
    [[nodiscard]] QRect visibleGeometry() const;
    [[nodiscard]] QRect hiddenGeometry() const;

    static constexpr int kBottomMargin = 20;
    static constexpr int kRevealHeight = 96;
    static constexpr int kAnimationDurationMs = 180;
    static constexpr int kCursorHideDelayMs = 2000;

    QWidget *window_ = nullptr;
    QWidget *revealZone_ = nullptr;
    FullscreenControlBar *controlBar_ = nullptr;
    QPropertyAnimation *animation_ = nullptr;
    QTimer *hideTimer_ = nullptr;
    QTimer *cursorTimer_ = nullptr;
    bool fullscreen_ = false;
    bool frameVisible_ = false;
    bool pointerInRevealZone_ = false;
    bool pointerInControlBar_ = false;
    bool targetVisible_ = false;
};
