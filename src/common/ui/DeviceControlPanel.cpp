#include "ui/DeviceControlPanel.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/VirtualJoystickWidget.h"

namespace {

QString oneLinePayload(const QByteArray &payload)
{
    QString text = QString::fromUtf8(payload);
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    for (qsizetype index = 0; index < text.size(); ++index) {
        const ushort value = text.at(index).unicode();
        if ((value < 0x20 || value == 0x7f) && value != '\\') {
            const QString escaped = QStringLiteral("\\u%1")
                .arg(value, 4, 16, QLatin1Char('0'));
            text.replace(index, 1, escaped);
            index += escaped.size() - 1;
        }
    }
    return text;
}

QFrame *card(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setProperty("styleRole", QStringLiteral("deviceControlCard"));
    return frame;
}

QPushButton *makeKeycap(const QString &text, const QString &accessibleName,
                        QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("styleRole", QStringLiteral("keycap"));
    button->setFocusPolicy(Qt::NoFocus);
    button->setAttribute(Qt::WA_TransparentForMouseEvents);
    button->setAccessibleName(accessibleName);
    button->setMinimumSize(66, 42);
    return button;
}

} // namespace

DeviceControlPanel::DeviceControlPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("deviceControlPanel"));
    setMinimumWidth(320);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *connectionCard = card(this);
    auto *connectionLayout = new QVBoxLayout(connectionCard);
    connectionLayout->setContentsMargins(12, 10, 12, 12);
    connectionLayout->setSpacing(7);

    auto *statusRow = new QHBoxLayout;
    statusDot_ = new QLabel(QStringLiteral("●"), connectionCard);
    statusDot_->setObjectName(QStringLiteral("mqttStatusDot"));
    statusDot_->setProperty("connectionState", QStringLiteral("offline"));
    statusDot_->setAccessibleName(tr("MQTT 连接状态指示"));
    statusLabel_ = new QLabel(tr("MQTT 未连接"), connectionCard);
    statusLabel_->setObjectName(QStringLiteral("mqttStatusLabel"));
    auto *settings = new QToolButton(connectionCard);
    settings->setObjectName(QStringLiteral("mqttSettingsButton"));
    settings->setText(tr("设置"));
    settings->setToolTip(tr("打开 MQTT Broker 和 Topic 设置"));
    settings->setAccessibleName(tr("MQTT 设置"));
    statusRow->addWidget(statusDot_);
    statusRow->addWidget(statusLabel_, 1);
    statusRow->addWidget(settings);
    connectionLayout->addLayout(statusRow);

    statusDetailLabel_ = new QLabel(connectionCard);
    statusDetailLabel_->setObjectName(QStringLiteral("mqttStatusDetailLabel"));
    statusDetailLabel_->setWordWrap(true);
    statusDetailLabel_->hide();
    connectionLayout->addWidget(statusDetailLabel_);

    topicLabel_ = new QLabel(tr("Topic：device/control"), connectionCard);
    topicLabel_->setObjectName(QStringLiteral("mqttTopicLabel"));
    topicLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connectionLayout->addWidget(topicLabel_);

    auto *targetRow = new QHBoxLayout;
    targetLabel_ = new QLabel(tr("控制目标：未选择"), connectionCard);
    targetLabel_->setObjectName(QStringLiteral("deviceControlTargetLabel"));
    targetStateLabel_ = new QLabel(tr("状态不可用"), connectionCard);
    targetStateLabel_->setObjectName(QStringLiteral("devicePresenceStateLabel"));
    targetStateLabel_->setProperty("presenceState", QStringLiteral("unavailable"));
    targetRow->addWidget(targetLabel_, 1);
    targetRow->addWidget(targetStateLabel_);
    connectionLayout->addLayout(targetRow);

    auto *streamRow = new QHBoxLayout;
    startStreamButton_ = new QPushButton(tr("发送启动推流"), connectionCard);
    startStreamButton_->setObjectName(QStringLiteral("startDeviceStreamButton"));
    startStreamButton_->setToolTip(tr("将当前视频地址写入 startStream.data.url 后提交"));
    stopStreamButton_ = new QPushButton(tr("发送停止推流"), connectionCard);
    stopStreamButton_->setObjectName(QStringLiteral("stopDeviceStreamButton"));
    stopStreamButton_->setToolTip(tr("向 Broker 提交 stopStream；不代表设备已执行"));
    connect(startStreamButton_, &QPushButton::clicked, this,
            [this] { emit commandPressed(DeviceCommand::StartStream); });
    connect(stopStreamButton_, &QPushButton::clicked, this,
            [this] { emit commandPressed(DeviceCommand::StopStream); });
    streamRow->addWidget(startStreamButton_);
    streamRow->addWidget(stopStreamButton_);
    connectionLayout->addLayout(streamRow);
    root->addWidget(connectionCard);

    auto *movementCard = card(this);
    auto *movementLayout = new QVBoxLayout(movementCard);
    movementLayout->setContentsMargins(12, 10, 12, 12);
    movementLayout->setSpacing(9);
    auto *movementTitle = new QLabel(tr("车辆移动"), movementCard);
    movementTitle->setProperty("styleRole", QStringLiteral("sectionTitle"));
    movementLayout->addWidget(movementTitle);

    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(0);
    mouseModeButton_ = new QPushButton(tr("鼠标摇杆"), movementCard);
    keyboardModeButton_ = new QPushButton(tr("键盘 WASD"), movementCard);
    mouseModeButton_->setObjectName(QStringLiteral("mouseControlModeButton"));
    keyboardModeButton_->setObjectName(QStringLiteral("keyboardControlModeButton"));
    mouseModeButton_->setProperty("styleRole", QStringLiteral("segmentLeft"));
    keyboardModeButton_->setProperty("styleRole", QStringLiteral("segmentRight"));
    mouseModeButton_->setCheckable(true);
    keyboardModeButton_->setCheckable(true);
    mouseModeButton_->setChecked(true);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(mouseModeButton_);
    modeGroup->addButton(keyboardModeButton_);
    modeRow->addWidget(mouseModeButton_);
    modeRow->addWidget(keyboardModeButton_);
    movementLayout->addLayout(modeRow);

    inputStack_ = new QStackedWidget(movementCard);
    inputStack_->setObjectName(QStringLiteral("deviceControlInputStack"));

    auto *mousePage = new QWidget(inputStack_);
    auto *mouseLayout = new QVBoxLayout(mousePage);
    mouseLayout->setContentsMargins(0, 6, 0, 0);
    joystick_ = new VirtualJoystickWidget(mousePage);
    joystick_->setControlEnabled(false);
    auto *joystickRow = new QHBoxLayout;
    joystickRow->addStretch();
    joystickRow->addWidget(joystick_);
    joystickRow->addStretch();
    mouseLayout->addLayout(joystickRow);
    auto *mouseHint = new QLabel(
        tr("按住摇杆拖向目标方向，松手或回到中心即停车"), mousePage);
    mouseHint->setObjectName(QStringLiteral("mouseControlHint"));
    mouseHint->setAlignment(Qt::AlignCenter);
    mouseHint->setWordWrap(true);
    mouseLayout->addWidget(mouseHint);
    inputStack_->addWidget(mousePage);

    auto *keyboardPage = new QWidget(inputStack_);
    auto *keyboardLayout = new QVBoxLayout(keyboardPage);
    keyboardLayout->setContentsMargins(0, 8, 0, 0);
    keyboardLayout->setSpacing(8);
    auto *keys = new QGridLayout;
    keys->setHorizontalSpacing(6);
    keys->setVerticalSpacing(6);
    forwardKey_ = makeKeycap(tr("W  /  ↑"), tr("前进键"), keyboardPage);
    leftKey_ = makeKeycap(tr("A  /  ←"), tr("左转键"), keyboardPage);
    backwardKey_ = makeKeycap(tr("S  /  ↓"), tr("后退键"), keyboardPage);
    rightKey_ = makeKeycap(tr("D  /  →"), tr("右转键"), keyboardPage);
    keys->addWidget(forwardKey_, 0, 1);
    keys->addWidget(leftKey_, 1, 0);
    keys->addWidget(backwardKey_, 1, 1);
    keys->addWidget(rightKey_, 1, 2);
    keyboardLayout->addLayout(keys);

    keyboardStatusLabel_ = new QLabel(tr("键盘控制：未启用"), keyboardPage);
    keyboardStatusLabel_->setObjectName(QStringLiteral("keyboardControlStatus"));
    keyboardStatusLabel_->setAlignment(Qt::AlignCenter);
    keyboardLayout->addWidget(keyboardStatusLabel_);
    keyboardArmButton_ = new QPushButton(tr("启用键盘控制"), keyboardPage);
    keyboardArmButton_->setObjectName(QStringLiteral("keyboardArmButton"));
    keyboardArmButton_->setCheckable(true);
    keyboardArmButton_->setToolTip(
        tr("启用后 WASD 和方向键在主窗口内控制车辆；Esc 解除"));
    keyboardLayout->addWidget(keyboardArmButton_);
    auto *keyboardHint = new QLabel(
        tr("最后按下的方向优先 · Space 立即停车 · Esc 解除控制"), keyboardPage);
    keyboardHint->setObjectName(QStringLiteral("keyboardControlHint"));
    keyboardHint->setAlignment(Qt::AlignCenter);
    keyboardHint->setWordWrap(true);
    keyboardLayout->addWidget(keyboardHint);
    keyboardLayout->addStretch();
    inputStack_->addWidget(keyboardPage);
    movementLayout->addWidget(inputStack_);

    stopCarButton_ = new QPushButton(tr("立即停车  ·  Space"), movementCard);
    stopCarButton_->setObjectName(QStringLiteral("stopCarButton"));
    stopCarButton_->setAccessibleName(tr("立即停车"));
    stopCarButton_->setToolTip(tr("立即向 Broker 提交 stopCar"));
    stopCarButton_->setMinimumHeight(38);
    movementLayout->addWidget(stopCarButton_);
    root->addWidget(movementCard);

    auto *safetyLabel = new QLabel(
        tr("安全提示 · 断网自动停车未经固件验证；网络中断时桌面端无法保证车辆停车"),
        this
    );
    safetyLabel->setObjectName(QStringLiteral("deviceSafetyNotice"));
    safetyLabel->setProperty("severity", QStringLiteral("warning"));
    safetyLabel->setWordWrap(true);
    root->addWidget(safetyLabel);

    resultLabel_ = new QLabel(this);
    resultLabel_->setObjectName(QStringLiteral("mqttResultLabel"));
    resultLabel_->setWordWrap(true);
    resultLabel_->hide();
    root->addWidget(resultLabel_);

    observedToggle_ = new QToolButton(this);
    observedToggle_->setObjectName(QStringLiteral("mqttObservedMessagesToggle"));
    observedToggle_->setText(tr("Topic 消息观察（最近 20 条，来源未知）"));
    observedToggle_->setCheckable(true);
    observedToggle_->setChecked(false);
    observedToggle_->setArrowType(Qt::RightArrow);
    observedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    observedToggle_->setAccessibleName(tr("展开 Topic 消息观察"));
    root->addWidget(observedToggle_);
    observedMessages_ = new QListWidget(this);
    observedMessages_->setObjectName(QStringLiteral("mqttObservedMessages"));
    observedMessages_->setMinimumHeight(140);
    observedMessages_->setMaximumHeight(220);
    observedMessages_->setAlternatingRowColors(true);
    observedMessages_->hide();
    root->addWidget(observedMessages_);
    root->addStretch();

    connect(joystick_, &VirtualJoystickWidget::commandPressed,
            this, &DeviceControlPanel::commandPressed);
    connect(joystick_, &VirtualJoystickWidget::movementReleased,
            this, &DeviceControlPanel::movementReleased);
    connect(mouseModeButton_, &QPushButton::clicked, this,
            [this] { selectKeyboardMode(false); });
    connect(keyboardModeButton_, &QPushButton::clicked, this,
            [this] { selectKeyboardMode(true); });
    connect(keyboardArmButton_, &QPushButton::clicked, this,
            [this](bool checked) { emit keyboardArmRequested(checked); });
    connect(stopCarButton_, &QPushButton::clicked, this, [this] {
        const bool joystickWasDriving = joystick_->isDriving();
        joystick_->cancelMovement();
        emit inputResetRequested();
        if (!joystickWasDriving) emit commandPressed(DeviceCommand::StopCar);
    });
    connect(settings, &QToolButton::clicked, this, [this] {
        cancelInteractiveControl();
        emit controlContextLost();
        emit settingsRequested();
    });
    connect(observedToggle_, &QToolButton::toggled, this, [this](bool expanded) {
        observedToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        observedToggle_->setAccessibleName(
            expanded ? tr("折叠 Topic 消息观察") : tr("展开 Topic 消息观察"));
        observedMessages_->setVisible(expanded);
    });

    setTabOrder(settings, startStreamButton_);
    setTabOrder(startStreamButton_, stopStreamButton_);
    setTabOrder(stopStreamButton_, mouseModeButton_);
    setTabOrder(mouseModeButton_, keyboardModeButton_);
    setTabOrder(keyboardModeButton_, joystick_);
    setTabOrder(joystick_, keyboardArmButton_);
    setTabOrder(keyboardArmButton_, stopCarButton_);
    setTabOrder(stopCarButton_, observedToggle_);
    updateCommandEnabled();
}

QSize DeviceControlPanel::sizeHint() const
{
    const QSize preferred = QWidget::sizeHint();
    return {qMax(360, preferred.width()), preferred.height()};
}

void DeviceControlPanel::setConnectionState(MqttConnectionState state,
                                             const QString &detail)
{
    connected_ = state == MqttConnectionState::Connected;
    QString name;
    QString visualState = QStringLiteral("offline");
    switch (state) {
    case MqttConnectionState::Disabled: name = tr("MQTT 已禁用"); break;
    case MqttConnectionState::Disconnected: name = tr("MQTT 未连接"); break;
    case MqttConnectionState::Connecting:
        name = tr("MQTT 正在连接"); visualState = QStringLiteral("pending"); break;
    case MqttConnectionState::Subscribing:
        name = tr("MQTT 正在订阅"); visualState = QStringLiteral("pending"); break;
    case MqttConnectionState::Connected:
        name = tr("MQTT 已连接并订阅"); visualState = QStringLiteral("online"); break;
    case MqttConnectionState::Reconnecting:
        name = tr("MQTT 正在重连"); visualState = QStringLiteral("pending"); break;
    case MqttConnectionState::Error:
        name = tr("MQTT 连接错误"); visualState = QStringLiteral("error"); break;
    }
    statusLabel_->setText(name);
    statusDetailLabel_->setText(detail);
    statusDetailLabel_->setVisible(!detail.isEmpty());
    statusDot_->setProperty("connectionState", visualState);
    statusDot_->style()->unpolish(statusDot_);
    statusDot_->style()->polish(statusDot_);
    if (!connected_) setKeyboardArmedState(false);
    updateCommandEnabled();
}

void DeviceControlPanel::setTopic(const QString &topic)
{
    setTopics(topic, QStringLiteral("device/status"));
}

void DeviceControlPanel::setTopics(const QString &controlTopic,
                                   const QString &statusTopic)
{
    topicLabel_->setText(tr("控制：%1\n状态：%2")
                             .arg(controlTopic, statusTopic));
    topicLabel_->setToolTip(tr("控制 Topic：%1\n状态 Topic：%2")
                                .arg(controlTopic, statusTopic));
}

void DeviceControlPanel::setControlTarget(const QString &deviceId,
                                          DevicePresenceState state)
{
    hasTarget_ = !deviceId.trimmed().isEmpty();
    targetLabel_->setText(hasTarget_
        ? tr("控制目标：%1").arg(deviceId.trimmed())
        : tr("控制目标：未选择"));
    setDevicePresenceState(hasTarget_ ? state
                                      : DevicePresenceState::Unavailable);
}

void DeviceControlPanel::setDevicePresenceState(DevicePresenceState state)
{
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
    targetOnline_ = state == DevicePresenceState::Online;
    targetStateLabel_->setText(QStringLiteral("● %1").arg(text));
    targetStateLabel_->setProperty("presenceState", name);
    targetStateLabel_->style()->unpolish(targetStateLabel_);
    targetStateLabel_->style()->polish(targetStateLabel_);
    updateCommandEnabled();
}

void DeviceControlPanel::appendObservedMessage(const MqttObservedMessage &message)
{
    const QString time = QDateTime::fromMSecsSinceEpoch(message.receivedAtMs)
        .toString(QStringLiteral("HH:mm:ss.zzz"));
    const bool truncated = message.originalPayloadSize > message.payload.size();
    const QString item = tr("%1 | 来源未知 | %2 | %3%4")
        .arg(time, message.topic, oneLinePayload(message.payload),
             truncated ? tr(" …（原始 %1 字节，已截断）")
                             .arg(message.originalPayloadSize)
                       : QString());
    observedMessages_->addItem(item);
    while (observedMessages_->count() > kMaximumObservedMessages)
        delete observedMessages_->takeItem(0);
    observedMessages_->scrollToBottom();
}

void DeviceControlPanel::showObservedMessagesDropped(quint64 count)
{
    setLastResult(tr("消息过快，部分观察消息已丢弃（%1 条）").arg(count), true);
}

void DeviceControlPanel::setLastResult(const QString &text, bool error)
{
    resultLabel_->setText(text);
    resultLabel_->setVisible(!text.isEmpty());
    resultLabel_->setProperty("error", error);
    resultLabel_->setProperty(
        "severity",
        error ? QStringLiteral("error") : QStringLiteral("success")
    );
    resultLabel_->style()->unpolish(resultLabel_);
    resultLabel_->style()->polish(resultLabel_);
}

void DeviceControlPanel::setKeyboardArmedState(bool armed)
{
    const QSignalBlocker blocker(keyboardArmButton_);
    keyboardArmButton_->setChecked(armed);
    keyboardArmButton_->setText(armed ? tr("解除键盘控制") : tr("启用键盘控制"));
    keyboardArmButton_->setProperty("armed", armed);
    keyboardStatusLabel_->setText(
        armed ? tr("键盘控制：已启用") : tr("键盘控制：未启用"));
    keyboardStatusLabel_->setProperty("armed", armed);
    keyboardArmButton_->style()->unpolish(keyboardArmButton_);
    keyboardArmButton_->style()->polish(keyboardArmButton_);
    keyboardStatusLabel_->style()->unpolish(keyboardStatusLabel_);
    keyboardStatusLabel_->style()->polish(keyboardStatusLabel_);
}

QPushButton *DeviceControlPanel::keyButton(DeviceCommand command) const
{
    switch (command) {
    case DeviceCommand::MoveForward: return forwardKey_;
    case DeviceCommand::MoveBackward: return backwardKey_;
    case DeviceCommand::TurnLeft: return leftKey_;
    case DeviceCommand::TurnRight: return rightKey_;
    default: return nullptr;
    }
}

void DeviceControlPanel::setKeyboardDirectionState(DeviceCommand command,
                                                   bool pressed)
{
    QPushButton *button = keyButton(command);
    if (button == nullptr) return;
    button->setProperty("pressed", pressed);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void DeviceControlPanel::cancelInteractiveControl()
{
    joystick_->cancelMovement();
    emit keyboardArmRequested(false);
}

bool DeviceControlPanel::event(QEvent *event)
{
    if (event != nullptr &&
        (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate)) {
        cancelInteractiveControl();
        emit controlContextLost();
    }
    return QWidget::event(event);
}

void DeviceControlPanel::selectKeyboardMode(bool keyboard)
{
    if (keyboardMode_ == keyboard) return;
    if (keyboard) joystick_->cancelMovement();
    else emit keyboardArmRequested(false);
    keyboardMode_ = keyboard;
    inputStack_->setCurrentIndex(keyboard ? 1 : 0);
    mouseModeButton_->setChecked(!keyboard);
    keyboardModeButton_->setChecked(keyboard);
    emit keyboardModeSelected(keyboard);
    updateCommandEnabled();
}

void DeviceControlPanel::updateCommandEnabled()
{
    const bool targetReady = connected_ && hasTarget_;
    startStreamButton_->setEnabled(targetReady && targetOnline_);
    stopStreamButton_->setEnabled(targetReady);
    stopCarButton_->setEnabled(targetReady);
    keyboardArmButton_->setEnabled(targetReady && targetOnline_ && keyboardMode_);
    joystick_->setControlEnabled(targetReady && targetOnline_ && !keyboardMode_);
}
