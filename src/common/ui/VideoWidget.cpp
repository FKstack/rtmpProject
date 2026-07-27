#include "ui/VideoWidget.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QMenu>
#include <QPalette>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr char kVideoWidgetMimeType[] = "application/x-rtmp-monitor-video-widget";
constexpr int kClickEffectDurationMs = 140;

class VideoSurface final : public QFrame
{
public:
    explicit VideoSurface(QWidget *parent)
        : QFrame(parent)
    {
    }

    void setFrame(const QImage &image)
    {
        frame_ = image;
        update();
    }

    void clearFrame()
    {
        frame_ = QImage();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);
        if (frame_.isNull()) {
            return;
        }

        QSize targetSize = frame_.size();
        targetSize.scale(size(), Qt::KeepAspectRatio);
        const QRect targetRect(
            QPoint((width() - targetSize.width()) / 2,
                   (height() - targetSize.height()) / 2),
            targetSize
        );

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(targetRect, frame_);
    }

private:
    QImage frame_;
};

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
    // 4x4 布局仍需允许主窗口合理缩放，最小尺寸只保证标题和状态文本可辨认。
    setMinimumSize(160, 100);
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
    videoSurface_ = new VideoSurface(this);
    videoSurface_->setObjectName(QStringLiteral("videoSurface"));
    videoSurface_->setProperty("styleRole", "videoSurface");
    videoSurface_->setFrameShape(QFrame::NoFrame);
    videoSurface_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoSurface_->setAttribute(Qt::WA_TransparentForMouseEvents);
    videoSurface_->setAttribute(Qt::WA_StyledBackground);
    videoSurface_->setAutoFillBackground(true);
    videoSurface_->installEventFilter(this);

    // 视频区域换 parent 后仍需独立保持黑色，不能依赖 VideoWidget 后代选择器。
    QPalette videoSurfacePalette = videoSurface_->palette();
    videoSurfacePalette.setColor(QPalette::Window, Qt::black);
    videoSurface_->setPalette(videoSurfacePalette);

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
    statusLabel_->setVisible(true);
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

void VideoWidget::displayFrame(const QImage &image)
{
    if (image.isNull()) {
        return;
    }
    static_cast<VideoSurface *>(videoSurface_)->setFrame(image);
    statusLabel_->setVisible(false);
}

void VideoWidget::clearFrame()
{
    static_cast<VideoSurface *>(videoSurface_)->clearFrame();
    statusLabel_->setVisible(true);
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

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
#ifndef NDEBUG
    qDebug() << "fullscreen event"
             << QDateTime::currentMSecsSinceEpoch()
             << this
             << event->type()
             << (dragEnabled_ ? "windowed-request" : "interaction-disabled");
#endif
    if (dragEnabled_ && event->button() == Qt::LeftButton) {
        mousePressed_ = false;
        setDragState(DragState::Idle);
        emit fullscreenRequested(this);
        event->accept();
        return;
    }

    QFrame::mouseDoubleClickEvent(event);
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

void VideoWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (!dragEnabled_) {
        event->ignore();
        return;
    }

    QMenu menu(this);
    QAction *reconnectAction = menu.addAction(tr("重新连接"));
    QAction *removeAction = menu.addAction(tr("断开并移除"));
    QAction *selected = menu.exec(event->globalPos());
    if (selected == reconnectAction) {
        emit reconnectRequested(this);
    } else if (selected == removeAction) {
        emit removeRequested(this);
    }
    event->accept();
}

bool VideoWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == videoSurface_ && event->type() == QEvent::Resize) {
        emit presentationTargetChanged(
            this, videoSurface_->size(), fullscreenSurfaceMode_
        );
    }
    return QFrame::eventFilter(watched, event);
}

void VideoWidget::startDrag()
{
    auto *drag = new QDrag(this);
    // 创建 MIME 数据，用于标识本次拖拽携带的数据类型。
    auto *mimeData = new QMimeData;
    // 添加自定义 MIME 数据，说明这是一个视频格拖拽。
    mimeData->setData(kVideoWidgetMimeType, QByteArrayLiteral("video-widget"));

    drag->setMimeData(mimeData);
    drag->setPixmap(grab());
    // 设置鼠标在拖拽图片中的位置。
    // 保证开始拖拽后，图片不会突然跳动。
    drag->setHotSpot(dragStartPosition_);

    setDragState(DragState::DragSource);
    // 启动拖放流程，只允许执行 MoveAction。
    // exec() 会一直等待，直到拖拽成功、取消或失败。
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

QFrame *VideoWidget::videoSurfaceForFullscreen() const noexcept
{
    return videoSurface_;
}

bool VideoWidget::isStatusLabelVisible() const noexcept
{
    return statusLabel_->isVisible();
}

void VideoWidget::setFullscreenSurfaceMode(bool active, bool restoreStatusLabelVisible)
{
    // 状态标签位于真实视频区域内；全屏时隐藏它，避免占用未来实际画面。
    statusLabel_->setVisible(active ? false : restoreStatusLabelVisible);
    fullscreenSurfaceMode_ = active;
    emit presentationTargetChanged(this, videoSurface_->size(), active);
}
