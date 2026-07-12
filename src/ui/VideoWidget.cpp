#include "ui/VideoWidget.h"

#include <QLabel>
#include <QPalette>
#include <QSizePolicy>
#include <QVBoxLayout>

VideoWidget::VideoWidget(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    setMinimumSize(240, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("deviceNameLabel"));

    videoSurface_ = new QFrame(this);
    videoSurface_->setObjectName(QStringLiteral("videoSurface"));
    videoSurface_->setFrameShape(QFrame::NoFrame);
    videoSurface_->setAutoFillBackground(true);
    videoSurface_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QPalette surfacePalette = videoSurface_->palette();
    surfacePalette.setColor(QPalette::Window, Qt::black);
    videoSurface_->setPalette(surfacePalette);

    auto *surfaceLayout = new QVBoxLayout(videoSurface_);
    surfaceLayout->setContentsMargins(12, 12, 12, 12);

    statusLabel_ = new QLabel(videoSurface_);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);

    surfaceLayout->addWidget(statusLabel_);
    layout->addWidget(titleLabel_);
    layout->addWidget(videoSurface_, 1);

    setStyleSheet(
        "QFrame { background-color: #202020; border: 1px solid #4a4a4a; }"
        "QLabel#deviceNameLabel { color: #f0f0f0; font-weight: 600; }"
        "QFrame#videoSurface { background-color: #000000; border: none; }"
        "QLabel#statusLabel { color: #bdbdbd; }"
    );

    setDeviceName(tr("未命名设备"));
    setStatusText(tr("未连接"));
}

void VideoWidget::setDeviceName(const QString &deviceName)
{
    titleLabel_->setText(deviceName);
}

void VideoWidget::setStatusText(const QString &statusText)
{
    statusLabel_->setText(statusText);
}

QString VideoWidget::deviceName() const
{
    return titleLabel_->text();
}

QString VideoWidget::statusText() const
{
    return statusLabel_->text();
}
