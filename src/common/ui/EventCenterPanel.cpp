#include "ui/EventCenterPanel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString severityText(SecurityEventSeverity severity)
{
    switch (severity) {
    case SecurityEventSeverity::Low: return QObject::tr("低");
    case SecurityEventSeverity::Medium: return QObject::tr("中");
    case SecurityEventSeverity::High: return QObject::tr("高");
    case SecurityEventSeverity::Critical: return QObject::tr("严重");
    }
    return {};
}

QString typeText(SecurityEventType type)
{
    switch (type) {
    case SecurityEventType::MqttConnectionLost: return QObject::tr("MQTT 连接中断");
    case SecurityEventType::DevicePresenceLost: return QObject::tr("设备心跳丢失");
    case SecurityEventType::VideoStreamLost: return QObject::tr("视频播放中断");
    case SecurityEventType::MediaServerUnhealthy: return QObject::tr("SRS 健康异常");
    case SecurityEventType::LocalControlPublishFailed: return QObject::tr("控制请求本地发送失败");
    case SecurityEventType::LocalSafetyStopPublishFailed: return QObject::tr("停车请求本地发送失败");
    case SecurityEventType::LocalSafetyStopUnavailable: return QObject::tr("停车请求本地无法提交");
    case SecurityEventType::ManualIncident: return QObject::tr("人工标记事件");
    }
    return {};
}

QString stateText(const SecurityEventRecord &event)
{
    switch (event.state) {
    case SecurityEventState::Open: return QObject::tr("待处理");
    case SecurityEventState::Acknowledged: return QObject::tr("已确认");
    case SecurityEventState::Resolved:
        return isSystemEvent(event.eventType)
            ? QObject::tr("平台观察到恢复") : QObject::tr("已解决");
    case SecurityEventState::Closed:
        return event.closeDisposition ==
                   CloseDisposition::ClosedWithoutObservedRecovery
            ? QObject::tr("已关闭（未观察到恢复）") : QObject::tr("已关闭");
    }
    return {};
}

QString localTime(const QDateTime &utc)
{
    return utc.isValid()
        ? utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("—");
}

} // namespace

EventCenterPanel::EventCenterPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("eventCenterPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);

    storageBanner_ = new QLabel(this);
    storageBanner_->setObjectName(QStringLiteral("eventStorageBanner"));
    storageBanner_->setWordWrap(true);
    storageBanner_->setProperty("severity", QStringLiteral("critical"));
    storageBanner_->hide();
    layout->addWidget(storageBanner_);

    auto *toolbar = new QHBoxLayout;
    filterCombo_ = new QComboBox(this);
    filterCombo_->setObjectName(QStringLiteral("eventFilterCombo"));
    filterCombo_->addItem(tr("活动事件"), false);
    filterCombo_->addItem(tr("已关闭"), true);
    createButton_ = new QPushButton(tr("手工标记事件"), this);
    createButton_->setObjectName(QStringLiteral("createManualIncidentButton"));
    toolbar->addWidget(new QLabel(tr("显示："), this));
    toolbar->addWidget(filterCombo_);
    toolbar->addStretch();
    toolbar->addWidget(createButton_);
    layout->addLayout(toolbar);

    table_ = new QTableWidget(0, 7, this);
    table_->setObjectName(QStringLiteral("eventCenterTable"));
    table_->setHorizontalHeaderLabels({
        tr("等级"), tr("状态"), tr("类型"), tr("资源"),
        tr("首次发生"), tr("最近发生"), tr("次数")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    auto *actions = new QHBoxLayout;
    acknowledgeButton_ = new QPushButton(tr("确认"), this);
    resolveButton_ = new QPushButton(tr("解决人工事件"), this);
    closeButton_ = new QPushButton(tr("关闭"), this);
    forceCloseButton_ = new QPushButton(tr("未恢复但关闭…"), this);
    acknowledgeButton_->setObjectName(QStringLiteral("acknowledgeEventButton"));
    resolveButton_->setObjectName(QStringLiteral("resolveManualEventButton"));
    closeButton_->setObjectName(QStringLiteral("closeEventButton"));
    forceCloseButton_->setObjectName(QStringLiteral("forceCloseEventButton"));
    actions->addStretch();
    actions->addWidget(acknowledgeButton_);
    actions->addWidget(resolveButton_);
    actions->addWidget(closeButton_);
    actions->addWidget(forceCloseButton_);
    layout->addLayout(actions);

    connect(filterCombo_, &QComboBox::currentIndexChanged,
            this, &EventCenterPanel::rebuildTable);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &EventCenterPanel::updateActions);
    connect(createButton_, &QPushButton::clicked,
            this, &EventCenterPanel::createManualIncident);
    connect(acknowledgeButton_, &QPushButton::clicked, this, [this] {
        const QString id = selectedEventId();
        if (!id.isEmpty()) emit acknowledgeRequested(id);
    });
    connect(resolveButton_, &QPushButton::clicked, this, [this] {
        const QString id = selectedEventId();
        if (!id.isEmpty()) emit resolveManualRequested(id);
    });
    connect(closeButton_, &QPushButton::clicked, this, [this] {
        const QString id = selectedEventId();
        if (!id.isEmpty()) emit closeRequested(id);
    });
    connect(forceCloseButton_, &QPushButton::clicked,
            this, &EventCenterPanel::forceCloseSelected);
    updateActions();
}

void EventCenterPanel::setEvents(const QList<SecurityEventRecord> &events,
                                 const EventCenterSummary &)
{
    events_ = events;
    rebuildTable();
}

void EventCenterPanel::setResources(
    const QList<EventResourceDescriptor> &resources)
{
    resources_ = resources;
}

void EventCenterPanel::setStorageState(bool writeEnabled, const QString &error)
{
    writeEnabled_ = writeEnabled;
    storageBanner_->setVisible(!writeEnabled);
    storageBanner_->setText(error.isEmpty()
        ? tr("平台事件存储不可写；播放和车辆安全控制不受影响。")
        : tr("平台事件存储不可写：%1").arg(error));
    createButton_->setEnabled(writeEnabled_);
    updateActions();
}

void EventCenterPanel::showOperationError(const QString &message)
{
    if (!message.trimmed().isEmpty())
        QMessageBox::warning(this, tr("事件操作未完成"), message);
}

void EventCenterPanel::rebuildTable()
{
    const QString selectedId = selectedEventId();
    const bool showClosed = filterCombo_->currentData().toBool();
    QList<SecurityEventRecord> visible;
    for (const auto &event : events_) {
        if ((event.state == SecurityEventState::Closed) == showClosed)
            visible.append(event);
    }
    std::sort(visible.begin(), visible.end(),
              [showClosed](const auto &left, const auto &right) {
        if (showClosed) return left.closedAtUtc > right.closedAtUtc;
        if (left.severity != right.severity)
            return static_cast<int>(left.severity) >
                   static_cast<int>(right.severity);
        return left.lastObservedAtUtc > right.lastObservedAtUtc;
    });

    table_->setRowCount(0);
    for (const auto &event : visible) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto *severity = new QTableWidgetItem(severityText(event.severity));
        severity->setData(Qt::UserRole, event.eventId);
        severity->setData(Qt::UserRole + 1, static_cast<int>(event.severity));
        if (event.severity == SecurityEventSeverity::Critical)
            severity->setForeground(QColor(QStringLiteral("#ff6b6b")));
        table_->setItem(row, 0, severity);
        table_->setItem(row, 1, new QTableWidgetItem(stateText(event)));
        table_->setItem(row, 2, new QTableWidgetItem(typeText(event.eventType)));
        table_->setItem(row, 3, new QTableWidgetItem(
            event.displayNameSnapshot.isEmpty()
                ? event.localResourceId : event.displayNameSnapshot));
        table_->setItem(row, 4, new QTableWidgetItem(localTime(event.openedAtUtc)));
        table_->setItem(row, 5, new QTableWidgetItem(localTime(event.lastObservedAtUtc)));
        table_->setItem(row, 6, new QTableWidgetItem(QString::number(event.occurrenceCount)));
        if (event.eventId == selectedId) table_->selectRow(row);
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    updateActions();
}

void EventCenterPanel::updateActions()
{
    const SecurityEventRecord *event = selectedEvent();
    const bool writable = writeEnabled_ && event != nullptr;
    acknowledgeButton_->setEnabled(
        writable && event->state == SecurityEventState::Open);
    resolveButton_->setEnabled(
        writable && event->eventType == SecurityEventType::ManualIncident &&
        event->state == SecurityEventState::Acknowledged);
    closeButton_->setEnabled(
        writable && event->state == SecurityEventState::Resolved);
    forceCloseButton_->setEnabled(
        writable && isSystemEvent(event->eventType) &&
        (event->state == SecurityEventState::Open ||
         event->state == SecurityEventState::Acknowledged));
}

void EventCenterPanel::createManualIncident()
{
    if (!writeEnabled_) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("手工标记平台事件"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    QComboBox severity;
    severity.addItem(tr("低"), static_cast<int>(SecurityEventSeverity::Low));
    severity.addItem(tr("中"), static_cast<int>(SecurityEventSeverity::Medium));
    severity.addItem(tr("高"), static_cast<int>(SecurityEventSeverity::High));
    severity.addItem(tr("严重"), static_cast<int>(SecurityEventSeverity::Critical));
    severity.setCurrentIndex(1);
    QComboBox resource;
    for (int index = 0; index < resources_.size(); ++index)
        resource.addItem(resources_.at(index).displayName, index);
    QTextEdit note;
    note.setPlaceholderText(tr("说明观察到的事实；不要填写密码、Token 或完整 URL。"));
    note.setAcceptRichText(false);
    form->addRow(tr("等级"), &severity);
    form->addRow(tr("资源"), &resource);
    form->addRow(tr("事件说明"), &note);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (resources_.isEmpty() || dialog.exec() != QDialog::Accepted) return;
    const QString description = note.toPlainText().trimmed();
    if (description.isEmpty()) {
        QMessageBox::warning(this, tr("缺少事件说明"),
                             tr("手工事件必须填写事件说明。"));
        return;
    }
    const int resourceIndex = resource.currentData().toInt();
    if (resourceIndex < 0 || resourceIndex >= resources_.size()) return;
    const auto &selected = resources_.at(resourceIndex);
    emit manualIncidentRequested(
        static_cast<SecurityEventSeverity>(severity.currentData().toInt()),
        selected.localResourceId, selected.deviceId, selected.displayName,
        selected.identitySource, description);
}

void EventCenterPanel::forceCloseSelected()
{
    const SecurityEventRecord *event = selectedEvent();
    if (event == nullptr || !isSystemEvent(event->eventType)) return;
    if (QMessageBox::warning(
            this, tr("未观察到恢复"),
            tr("平台尚未观察到该故障恢复。继续关闭只表示结束本地事件，"
               "不表示问题已经解决。是否继续？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) return;
    bool accepted = false;
    const QString reason = QInputDialog::getMultiLineText(
        this, tr("填写关闭原因"), tr("关闭原因（必填）"), {}, &accepted).trimmed();
    if (!accepted) return;
    if (reason.isEmpty()) {
        QMessageBox::warning(this, tr("缺少关闭原因"),
                             tr("未观察到恢复时必须填写关闭原因。"));
        return;
    }
    emit forceCloseRequested(event->eventId, reason);
}

QString EventCenterPanel::selectedEventId() const
{
    const int row = table_->currentRow();
    QTableWidgetItem *item = row >= 0 ? table_->item(row, 0) : nullptr;
    return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

const SecurityEventRecord *EventCenterPanel::selectedEvent() const
{
    const QString id = selectedEventId();
    for (const auto &event : events_) {
        if (event.eventId == id) return &event;
    }
    return nullptr;
}
