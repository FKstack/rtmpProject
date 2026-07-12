#pragma once

#include <QFrame>
#include <QString>

class QLabel;

class VideoWidget final : public QFrame
{
public:
    explicit VideoWidget(QWidget *parent = nullptr);

    void setDeviceName(const QString &deviceName);
    void setStatusText(const QString &statusText);

    [[nodiscard]] QString deviceName() const;
    [[nodiscard]] QString statusText() const;

private:
    QLabel *titleLabel_ = nullptr;
    QFrame *videoSurface_ = nullptr;
    QLabel *statusLabel_ = nullptr;
};
