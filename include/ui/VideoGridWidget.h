#pragma once

#include <array>

#include <QWidget>

class VideoWidget;

class VideoGridWidget final : public QWidget
{
public:
    static constexpr int kVideoWidgetCount = 4;

    explicit VideoGridWidget(QWidget *parent = nullptr);

    [[nodiscard]] int videoWidgetCount() const noexcept;
    [[nodiscard]] VideoWidget *videoWidgetAt(int index) const noexcept;

private:
    std::array<VideoWidget *, kVideoWidgetCount> videoWidgets_{};
};
