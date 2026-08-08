#include "ui/VideoWidget.h"

#include <algorithm>
#include <utility>

#include <QApplication>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr char kVideoWidgetMimeType[] = "application/x-rtmp-monitor-video-widget";
constexpr int kClickEffectDurationMs = 140;
constexpr int kTitleOverlayInset = 6;

class VideoSurface final : public QFrame
{
public:
    explicit VideoSurface(QWidget *parent)
        : QFrame(parent)
    {
    }
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
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true);
    setAutoFillBackground(false);
    // 该属性是应用级 QSS 的稳定边界，避免视频格样式泄漏到后续其他 QFrame 控件。
    setProperty("styleRole", "videoWidget");
    setProperty("dragState", "idle");

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(0, 0, 0, 0);
    rootLayout_->setSpacing(0);

    // 该区域只提供共享画布的几何锚点；像素不再由每路 QWidget 保存或绘制。
    videoSurface_ = new VideoSurface(this);
    videoSurface_->setObjectName(QStringLiteral("videoSurface"));
    videoSurface_->setProperty("styleRole", "videoSurface");
    videoSurface_->setFrameShape(QFrame::NoFrame);
    videoSurface_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoSurface_->setAttribute(Qt::WA_TransparentForMouseEvents);
    videoSurface_->setAttribute(Qt::WA_StyledBackground, false);
    videoSurface_->setAutoFillBackground(false);

    // 视频区域换 parent 后仍需独立保持黑色，不能依赖 VideoWidget 后代选择器。
    videoSurface_->setAttribute(Qt::WA_NoSystemBackground);

    titleLabel_ = new QLabel(videoSurface_);
    titleLabel_->setObjectName(QStringLiteral("deviceNameLabel"));
    titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    titleLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    auto *surfaceLayout = new QVBoxLayout(videoSurface_);
    surfaceLayout->setContentsMargins(6, 6, 6, 6);

    statusLabel_ = new QLabel(videoSurface_);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);

    surfaceLayout->addWidget(statusLabel_);
    rootLayout_->addWidget(videoSurface_, 1);

    setDeviceName(tr("未命名设备"));
    setStatusText(tr("未连接"));
    updateMonitoringMinimumSize();
}

void VideoWidget::setDeviceName(const QString &deviceName)
{
    deviceName_ = deviceName;
    titleLabel_->setToolTip(deviceName);
    updateTitleOverlay();
}

void VideoWidget::setStatusText(const QString &statusText)
{
    statusLabel_->setText(statusText);
    statusLabel_->setVisible(true);
}

QString VideoWidget::deviceName() const
{
    return deviceName_;
}

QString VideoWidget::statusText() const
{
    return statusLabel_->text();
}

bool VideoWidget::isDragEnabled() const noexcept
{
    return dragEnabled_;
}

void VideoWidget::bindRenderSource(
    StreamId streamId,
    std::shared_ptr<LatestFrameMailbox> mailbox
)
{
    streamId_ = streamId;
    frameMailbox_ = std::move(mailbox);
    frameVisible_ = false;
    emit renderStateChanged(this);
}

void VideoWidget::unbindRenderSource()
{
    streamId_ = kInvalidStreamId;
    frameMailbox_.reset();
    frameVisible_ = false;
    emit renderStateChanged(this);
}

StreamId VideoWidget::streamId() const noexcept
{
    return streamId_;
}

std::shared_ptr<LatestFrameMailbox> VideoWidget::frameMailbox() const
{
    return frameMailbox_;
}

QRect VideoWidget::videoViewportRect(const QWidget *ancestor) const
{
    if (ancestor == nullptr || videoSurface_ == nullptr) {
        return {};
    }
    const QPoint topLeft = videoSurface_->mapTo(
        const_cast<QWidget *>(ancestor), QPoint(0, 0)
    );
    return QRect(topLeft, videoSurface_->size());
}

QSize VideoWidget::videoChromeSizeHint() const
{
    if (rootLayout_ == nullptr) {
        return {};
    }
    const QMargins margins = rootLayout_->contentsMargins();
    const int frameChrome = frameWidth() * 2;
    return {
        margins.left() + margins.right() + frameChrome,
        margins.top() + margins.bottom() + frameChrome,
    };
}

bool VideoWidget::isFrameVisible() const noexcept
{
    return frameVisible_;
}

VideoDisplayMode VideoWidget::displayMode() const noexcept
{
    return displayMode_;
}

void VideoWidget::setDisplayMode(VideoDisplayMode mode)
{
    if (displayMode_ == mode) {
        return;
    }
    displayMode_ = mode;
    emit renderStateChanged(this);
}

void VideoWidget::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event != nullptr &&
        (event->type() == QEvent::FontChange ||
         event->type() == QEvent::StyleChange)) {
        updateMonitoringMinimumSize();
        updateTitleOverlay();
    }
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateTitleOverlay();
}

void VideoWidget::showFrame()
{
    frameVisible_ = true;
    statusLabel_->setVisible(false);
    emit renderStateChanged(this);
}

void VideoWidget::clearFrame()
{
    frameVisible_ = false;
    if (frameMailbox_ != nullptr) {
        frameMailbox_->clear();
    }
    statusLabel_->setVisible(true);
    emit renderStateChanged(this);
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
    QMenu *displayModeMenu = menu.addMenu(tr("显示方式"));
    QActionGroup displayModeGroup(&menu);
    displayModeGroup.setExclusive(true);
    QAction *containAction = displayModeMenu->addAction(
        tr("完整显示（默认，标准 16:9 铺满）")
    );
    containAction->setCheckable(true);
    containAction->setChecked(displayMode_ == VideoDisplayMode::Contain);
    containAction->setToolTip(
        tr("保持原始比例并显示完整画面，比例不同时会保留黑边。")
    );
    displayModeGroup.addAction(containAction);
    QAction *coverAction = displayModeMenu->addAction(
        tr("裁剪铺满（可能丢失边缘）")
    );
    coverAction->setCheckable(true);
    coverAction->setChecked(displayMode_ == VideoDisplayMode::Cover);
    coverAction->setToolTip(
        tr("保持原始比例并铺满视频区域，比例不同时会从中心裁剪画面。")
    );
    displayModeGroup.addAction(coverAction);
    menu.addSeparator();
    QAction *reconnectAction = menu.addAction(tr("重新连接"));
    QAction *removeAction = menu.addAction(tr("断开并移除"));
    QAction *selected = menu.exec(event->globalPos());
    if (selected == coverAction) {
        setDisplayMode(VideoDisplayMode::Cover);
    } else if (selected == containAction) {
        setDisplayMode(VideoDisplayMode::Contain);
    } else if (selected == reconnectAction) {
        emit reconnectRequested(this);
    } else if (selected == removeAction) {
        emit removeRequested(this);
    }
    event->accept();
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

void VideoWidget::updateMonitoringMinimumSize()
{
    constexpr QSize kMinimumVideoViewport(144, 81);
    const QSize chrome = videoChromeSizeHint();
    setMinimumSize(kMinimumVideoViewport.width() + chrome.width(),
                   kMinimumVideoViewport.height() + chrome.height());
}

void VideoWidget::updateTitleOverlay()
{
    if (titleLabel_ == nullptr || videoSurface_ == nullptr) {
        return;
    }

    const int availableWidth = std::max(
        0, videoSurface_->width() - kTitleOverlayInset * 2
    );
    if (availableWidth <= 0) {
        titleLabel_->hide();
        return;
    }

    titleLabel_->setText(
        titleLabel_->fontMetrics().elidedText(
            deviceName_, Qt::ElideRight, availableWidth
        )
    );
    titleLabel_->adjustSize();
    titleLabel_->setGeometry(
        kTitleOverlayInset,
        kTitleOverlayInset,
        std::min(availableWidth, titleLabel_->sizeHint().width()),
        titleLabel_->sizeHint().height()
    );
    titleLabel_->show();
    titleLabel_->raise();
}
