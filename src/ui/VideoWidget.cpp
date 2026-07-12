#include "ui/VideoWidget.h"

#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

VideoWidget::VideoWidget(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    setMinimumSize(240, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 该属性是应用级 QSS 的稳定边界，避免视频格样式泄漏到后续其他 QFrame 控件。
    setProperty("styleRole", "videoWidget");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("deviceNameLabel"));

    // 将视频区域与标题、状态文本分离，后续可替换为 QImage 或 OpenGL 渲染而不改变外层布局。
    videoSurface_ = new QFrame(this);
    videoSurface_->setObjectName(QStringLiteral("videoSurface"));
    videoSurface_->setFrameShape(QFrame::NoFrame);
    videoSurface_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *surfaceLayout = new QVBoxLayout(videoSurface_);
    surfaceLayout->setContentsMargins(12, 12, 12, 12);

    statusLabel_ = new QLabel(videoSurface_);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);

    surfaceLayout->addWidget(statusLabel_);
    layout->addWidget(titleLabel_);
    layout->addWidget(videoSurface_, 1);

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
