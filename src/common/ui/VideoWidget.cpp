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
#include <QToolButton>
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

    presenceBadge_ = new QLabel(videoSurface_);
    presenceBadge_->setObjectName(QStringLiteral("devicePresenceBadge"));
    presenceBadge_->setAttribute(Qt::WA_TransparentForMouseEvents);
    presenceBadge_->setAlignment(Qt::AlignCenter);

    audioButton_ = new QToolButton(videoSurface_);
    audioButton_->setObjectName(QStringLiteral("audioToggleButton"));
    audioButton_->setProperty("styleRole", "audioToggle");
    audioButton_->setFocusPolicy(Qt::StrongFocus);
    audioButton_->setMinimumSize(40, 32);
    connect(audioButton_, &QToolButton::clicked, this, [this] {
        emit audioToggleRequested(this);
    });

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
    setAudioPlaybackState(AudioPlaybackState::Unavailable, false);
    setDevicePresenceState(DevicePresenceState::Unavailable);
    updateMonitoringMinimumSize();
}

void VideoWidget::setDeviceName(const QString &deviceName)
{
    if (deviceName_ == deviceName) {
        return;
    }
    deviceName_ = deviceName;
    titleLabel_->setToolTip(deviceName);
    updateTitleOverlay();
    emit renderStateChanged(this);
}

void VideoWidget::setStatusText(const QString &statusText)
{
    if (statusLabel_->text() == statusText) {
        statusLabel_->setVisible(true);
        return;
    }
    statusLabel_->setText(statusText);
    statusLabel_->setVisible(true);
    emit renderStateChanged(this);
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

void VideoWidget::setTitleOverlayEnabled(bool enabled)
{
    titleOverlayEnabled_ = enabled;
    updateTitleOverlay();
}

void VideoWidget::setAudioPlaybackState(
    AudioPlaybackState state,
    bool selected
)
{
    audioState_ = state;
    audioSelected_ = selected;
    QString text;
    QString toolTip;
    switch (state) {
    case AudioPlaybackState::Unavailable:
        text = tr("无音频");
        toolTip = tr("该视频流没有可播放的 AAC 音频");
        break;
    case AudioPlaybackState::Buffering:
        text = tr("缓冲");
        toolTip = tr("音频正在缓冲");
        break;
    case AudioPlaybackState::Playing:
        text = tr("静音");
        toolTip = tr("点击静音此路音频");
        break;
    case AudioPlaybackState::Muted:
        text = tr("开启声音");
        toolTip = selected ? tr("点击恢复此路音频") : tr("点击播放此路音频");
        break;
    case AudioPlaybackState::OutputError:
        text = tr("音频错误");
        toolTip = tr("默认音频输出设备不可用");
        break;
    }
    audioButton_->setText(text);
    audioButton_->setToolTip(toolTip);
    audioButton_->setEnabled(state != AudioPlaybackState::Unavailable);
    audioButton_->setProperty("audioState", static_cast<int>(state));
    audioButton_->setProperty("audioSelected", selected);
    audioButton_->style()->unpolish(audioButton_);
    audioButton_->style()->polish(audioButton_);
    updateTitleOverlay();
}

AudioPlaybackState VideoWidget::audioPlaybackState() const noexcept
{
    return audioState_;
}

bool VideoWidget::isAudioSelected() const noexcept
{
    return audioSelected_;
}

void VideoWidget::setDevicePresenceState(DevicePresenceState state)
{
    presenceState_ = state;
    QString text;
    QString name;
    switch (state) {
    case DevicePresenceState::Unavailable:
        text = tr("状态不可用"); name = QStringLiteral("unavailable"); break;
    case DevicePresenceState::Waiting:
        text = tr("等待心跳"); name = QStringLiteral("waiting"); break;
    case DevicePresenceState::Online:
        text = tr("在线"); name = QStringLiteral("online"); break;
    case DevicePresenceState::Offline:
        text = tr("离线"); name = QStringLiteral("offline"); break;
    }
    presenceBadge_->setText(QStringLiteral("● %1").arg(text));
    presenceBadge_->setProperty("presenceState", name);
    presenceBadge_->style()->unpolish(presenceBadge_);
    presenceBadge_->style()->polish(presenceBadge_);
    updateTitleOverlay();
}

DevicePresenceState VideoWidget::devicePresenceState() const noexcept
{
    return presenceState_;
}

void VideoWidget::setControlTargetSelected(bool selected)
{
    if (controlTargetSelected_ == selected) return;
    controlTargetSelected_ = selected;
    setProperty("controlSelected", selected);
    refreshStyle();
}

bool VideoWidget::isControlTargetSelected() const noexcept
{
    return controlTargetSelected_;
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
        emit controlTargetRequested(this);
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
    if (!titleOverlayEnabled_) {
        titleLabel_->hide();
        audioButton_->hide();
        presenceBadge_->hide();
        return;
    }
    if (titleLabel_ == nullptr || videoSurface_ == nullptr) {
        return;
    }

    audioButton_->adjustSize();
    presenceBadge_->adjustSize();
    const int buttonWidth = std::max(40, audioButton_->sizeHint().width());
    const int presenceWidth = std::max(58, presenceBadge_->sizeHint().width());
    audioButton_->setGeometry(
        std::max(kTitleOverlayInset,
                 videoSurface_->width() - kTitleOverlayInset - buttonWidth),
        kTitleOverlayInset,
        buttonWidth,
        std::max(32, audioButton_->sizeHint().height())
    );
    presenceBadge_->setGeometry(
        std::max(kTitleOverlayInset,
                 videoSurface_->width() - kTitleOverlayInset * 2 -
                     buttonWidth - presenceWidth),
        kTitleOverlayInset,
        presenceWidth,
        std::max(32, presenceBadge_->sizeHint().height())
    );
    audioButton_->show();
    audioButton_->raise();
    presenceBadge_->show();
    presenceBadge_->raise();
    const int availableWidth = std::max(
        0, videoSurface_->width() - kTitleOverlayInset * 4 - buttonWidth -
               presenceWidth
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
