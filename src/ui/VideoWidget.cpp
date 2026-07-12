#include "ui/VideoWidget.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr char kVideoWidgetMimeType[] = "application/x-rtmp-monitor-video-widget";
constexpr int kClickEffectDurationMs = 140;

/**
 * @brief 判断拖放事件是否来自同一进程中的另一个视频格。
 */
bool isVideoWidgetDrag(const QDropEvent *event, const VideoWidget *target)
{
    const auto *source = qobject_cast<VideoWidget *>(event->source());
    return source != nullptr && source != target &&
           event->mimeData()->hasFormat(kVideoWidgetMimeType);
}

} // namespace

VideoWidget::VideoWidget(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    setMinimumSize(240, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true);
    // 该属性是应用级 QSS 的稳定边界，避免视频格样式泄漏到后续其他 QFrame 控件。
    setProperty("styleRole", "videoWidget");
    setProperty("dragState", "idle");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("deviceNameLabel"));
    titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);

    // 将视频区域与标题、状态文本分离，后续可替换为 QImage 或 OpenGL 渲染而不改变外层布局。
    videoSurface_ = new QFrame(this);
    videoSurface_->setObjectName(QStringLiteral("videoSurface"));
    videoSurface_->setFrameShape(QFrame::NoFrame);
    videoSurface_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoSurface_->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *surfaceLayout = new QVBoxLayout(videoSurface_);
    surfaceLayout->setContentsMargins(12, 12, 12, 12);

    statusLabel_ = new QLabel(videoSurface_);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);

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

bool VideoWidget::isDragEnabled() const noexcept
{
    return dragEnabled_;
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (!dragEnabled_ || event->button() != Qt::LeftButton) {
        QFrame::mousePressEvent(event);
        return;
    }

    dragStartPosition_ = event->pos();
    mousePressed_ = true;
    setDragState(DragState::Pressed);
    event->accept();
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragEnabled_ || !mousePressed_ || !(event->buttons() & Qt::LeftButton) ||
        (event->pos() - dragStartPosition_).manhattanLength() < QApplication::startDragDistance()) {
        QFrame::mouseMoveEvent(event);
        return;
    }

    mousePressed_ = false;
    startDrag();
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && mousePressed_) {
        mousePressed_ = false;
        // 点击反馈保持一个短暂高亮，随后恢复空闲状态；实际拖拽会由拖放流程管理状态。
        QTimer::singleShot(kClickEffectDurationMs, this, [this] {
            if (dragState_ == DragState::Pressed) {
                setDragState(DragState::Idle);
            }
        });
        event->accept();
        return;
    }

    QFrame::mouseReleaseEvent(event);
}

void VideoWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (!dragEnabled_ || !isVideoWidgetDrag(event, this)) {
        event->ignore();
        return;
    }

    setDragState(DragState::DragTarget);
    event->acceptProposedAction();
}

void VideoWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (!dragEnabled_ || !isVideoWidgetDrag(event, this)) {
        event->ignore();
        return;
    }

    setDragState(DragState::DragTarget);
    event->acceptProposedAction();
}

void VideoWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    if (dragState_ == DragState::DragTarget) {
        setDragState(DragState::Idle);
    }

    event->accept();
}

void VideoWidget::dropEvent(QDropEvent *event)
{
    auto *source = qobject_cast<VideoWidget *>(event->source());
    if (!dragEnabled_ || source == nullptr || source == this || !source->isDragEnabled() ||
        !event->mimeData()->hasFormat(kVideoWidgetMimeType)) {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
    emit swapRequested(source, this);
}

void VideoWidget::startDrag()
{
    auto *drag = new QDrag(this);
    auto *mimeData = new QMimeData;
    mimeData->setData(kVideoWidgetMimeType, QByteArrayLiteral("video-widget"));

    drag->setMimeData(mimeData);
    drag->setPixmap(grab());
    drag->setHotSpot(dragStartPosition_);

    setDragState(DragState::DragSource);
    const Qt::DropAction action = drag->exec(Qt::MoveAction);
    if (action != Qt::MoveAction && dragState_ == DragState::DragSource) {
        setDragState(DragState::Idle);
    }
}

void VideoWidget::setDragEnabled(bool enabled)
{
    dragEnabled_ = enabled;
    mousePressed_ = false;

    if (!dragEnabled_) {
        setDragState(DragState::Idle);
    }
}

void VideoWidget::setDragState(DragState state)
{
    if (dragState_ == state) {
        return;
    }

    dragState_ = state;
    QString stateName;
    switch (state) {
    case DragState::Idle:
        stateName = QStringLiteral("idle");
        break;
    case DragState::Pressed:
        stateName = QStringLiteral("pressed");
        break;
    case DragState::DragSource:
        stateName = QStringLiteral("dragSource");
        break;
    case DragState::DragTarget:
        stateName = QStringLiteral("dragTarget");
        break;
    }

    setProperty("dragState", stateName);
    refreshStyle();
}

void VideoWidget::refreshStyle()
{
    // QSS 动态属性变化不会自动重新匹配选择器，因此在状态切换时显式刷新当前控件。
    style()->unpolish(this);
    style()->polish(this);
    update();
}
