#include "ui/FullscreenControlBar.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>

FullscreenControlBar::FullscreenControlBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("fullscreenControlBar"));
    setProperty("styleRole", "fullscreenControlBar");
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    setMouseTracking(true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(10);

    deviceNameLabel_ = new QLabel(this);
    deviceNameLabel_->setObjectName(QStringLiteral("fullscreenDeviceNameLabel"));

    streamInfoLabel_ = new QLabel(this);
    streamInfoLabel_->setObjectName(QStringLiteral("fullscreenStreamInfoLabel"));

    muteButton_ = new QPushButton(tr("静音"), this);
    muteButton_->setObjectName(QStringLiteral("muteButton"));
    muteButton_->setFocusPolicy(Qt::NoFocus);

    screenshotButton_ = new QPushButton(tr("截图"), this);
    screenshotButton_->setObjectName(QStringLiteral("screenshotButton"));
    screenshotButton_->setFocusPolicy(Qt::NoFocus);

    exitButton_ = new QPushButton(tr("退出全屏"), this);
    exitButton_->setObjectName(QStringLiteral("exitFullscreenButton"));
    exitButton_->setFocusPolicy(Qt::NoFocus);

    layout->addWidget(deviceNameLabel_);
    layout->addWidget(streamInfoLabel_);
    layout->addSpacing(8);
    layout->addWidget(muteButton_);
    layout->addWidget(screenshotButton_);
    layout->addWidget(exitButton_);

    connect(exitButton_, &QPushButton::clicked, this, &FullscreenControlBar::exitRequested);
    connect(muteButton_, &QPushButton::clicked, this, &FullscreenControlBar::muteRequested);
    connect(screenshotButton_, &QPushButton::clicked,
            this, &FullscreenControlBar::screenshotRequested);
}

void FullscreenControlBar::setDeviceName(const QString &deviceName)
{
    deviceNameLabel_->setText(deviceName);
}

void FullscreenControlBar::setStreamInfo(const QString &streamInfo)
{
    streamInfoLabel_->setText(streamInfo);
}

void FullscreenControlBar::enterEvent(QEnterEvent *event)
{
    emit pointerEntered();
    QFrame::enterEvent(event);
}

void FullscreenControlBar::leaveEvent(QEvent *event)
{
    emit pointerLeft();
    QFrame::leaveEvent(event);
}
