#include "ui/EventDetailDialog.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString localTime(const QDateTime &utc)
{
    return utc.isValid()
        ? utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
        : QStringLiteral("—");
}

QString availabilityText(EvidenceAvailability value)
{
    switch (value) {
    case EvidenceAvailability::Available: return QObject::tr("可用");
    case EvidenceAvailability::Missing: return QObject::tr("文件缺失");
    case EvidenceAvailability::UnsafePath: return QObject::tr("路径不安全");
    }
    return QObject::tr("不可用");
}

QString failureText(EvidenceCaptureFailure value)
{
    if (value == EvidenceCaptureFailure::None) return QObject::tr("成功");
    return evidenceCaptureFailureName(value);
}

} // namespace

EventDetailDialog::EventDetailDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("eventDetailDialog"));
    setWindowTitle(tr("平台事件详情"));
    resize(920, 720);
    auto *layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("eventDetailSummary"));
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    noticeLabel_ = new QLabel(
        tr("截图和导出是本机事件资料，不证明设备已执行、来源真实性或防篡改；本模块不进行内容哈希校验。"),
        this);
    noticeLabel_->setObjectName(QStringLiteral("evidenceHonestyNotice"));
    noticeLabel_->setWordWrap(true);
    layout->addWidget(noticeLabel_);

    storageBanner_ = new QLabel(this);
    storageBanner_->setObjectName(QStringLiteral("evidenceStorageBanner"));
    storageBanner_->setWordWrap(true);
    storageBanner_->setProperty("severity", QStringLiteral("critical"));
    storageBanner_->hide();
    layout->addWidget(storageBanner_);

    historyTable_ = new QTableWidget(0, 4, this);
    historyTable_->setObjectName(QStringLiteral("eventHistoryTable"));
    historyTable_->setHorizontalHeaderLabels(
        {tr("时间"), tr("转换"), tr("来源"), tr("说明")});
    historyTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    historyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(new QLabel(tr("状态历史"), this));
    layout->addWidget(historyTable_, 1);

    controlTable_ = new QTableWidget(0, 4, this);
    controlTable_->setObjectName(QStringLiteral("eventControlAttemptTable"));
    controlTable_->setHorizontalHeaderLabels(
        {tr("时间"), tr("动作"), tr("本地结果"), tr("执行确认")});
    controlTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    controlTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(new QLabel(tr("关联控制尝试"), this));
    layout->addWidget(controlTable_, 1);

    evidenceTable_ = new QTableWidget(0, 6, this);
    evidenceTable_->setObjectName(QStringLiteral("eventEvidenceTable"));
    evidenceTable_->setHorizontalHeaderLabels(
        {tr("证据 ID"), tr("捕获时间"), tr("流"), tr("帧龄"), tr("大小"), tr("状态")});
    evidenceTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    evidenceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(new QLabel(tr("截图证据"), this));
    layout->addWidget(evidenceTable_, 1);

    attemptTable_ = new QTableWidget(0, 4, this);
    attemptTable_->setObjectName(QStringLiteral("evidenceAttemptTable"));
    attemptTable_->setHorizontalHeaderLabels(
        {tr("请求时间"), tr("流"), tr("帧龄"), tr("结果")});
    attemptTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    attemptTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(new QLabel(tr("截图尝试"), this));
    layout->addWidget(attemptTable_, 1);

    auto *captureRow = new QHBoxLayout;
    sourceCombo_ = new QComboBox(this);
    sourceCombo_->setObjectName(QStringLiteral("evidenceSourceCombo"));
    captureButton_ = new QPushButton(tr("采集截图证据"), this);
    captureButton_->setObjectName(QStringLiteral("captureEvidenceButton"));
    exportButton_ = new QPushButton(tr("导出事件目录…"), this);
    exportButton_->setObjectName(QStringLiteral("exportIncidentButton"));
    auto *closeButton = new QPushButton(tr("关闭窗口"), this);
    captureRow->addWidget(new QLabel(tr("证据来源："), this));
    captureRow->addWidget(sourceCombo_, 1);
    captureRow->addWidget(captureButton_);
    captureRow->addWidget(exportButton_);
    captureRow->addWidget(closeButton);
    layout->addLayout(captureRow);

    connect(sourceCombo_, &QComboBox::currentIndexChanged,
            this, &EventDetailDialog::updateActions);
    connect(captureButton_, &QPushButton::clicked, this, [this] {
        emit captureRequested(event_.eventId,
                              sourceCombo_->currentData().toString());
    });
    connect(exportButton_, &QPushButton::clicked,
            this, &EventDetailDialog::chooseExportDirectory);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void EventDetailDialog::setEvent(const SecurityEventRecord &event)
{
    event_ = event;
    rebuild();
}

void EventDetailDialog::setEvidence(const QList<EvidenceRecord> &records,
                                    const QList<EvidenceCaptureAttempt> &attempts)
{
    records_.clear();
    attempts_.clear();
    for (const auto &record : records)
        if (record.eventId == event_.eventId) records_.append(record);
    for (const auto &attempt : attempts)
        if (attempt.eventId == event_.eventId) attempts_.append(attempt);
    rebuild();
}

void EventDetailDialog::setCaptureResources(
    const QList<EventResourceDescriptor> &resources)
{
    resources_ = resources;
    sourceCombo_->clear();
    int preferred = -1;
    for (int index = 0; index < resources_.size(); ++index) {
        const auto &resource = resources_.at(index);
        sourceCombo_->addItem(resource.displayName, resource.localResourceId);
        if (resource.localResourceId == event_.localResourceId ||
            (!event_.deviceId.isEmpty() && resource.deviceId == event_.deviceId)) {
            preferred = index;
        }
    }
    if (preferred >= 0) sourceCombo_->setCurrentIndex(preferred);
    updateActions();
}

void EventDetailDialog::setStorageState(bool eventWritable,
                                        bool evidenceWritable,
                                        const QString &evidenceError)
{
    eventWritable_ = eventWritable;
    evidenceWritable_ = evidenceWritable;
    storageBanner_->setVisible(!evidenceWritable);
    storageBanner_->setText(evidenceError.isEmpty()
        ? tr("证据存储不可写；播放、车辆安全控制和事件查看不受影响。")
        : tr("证据存储不可写：%1").arg(evidenceError));
    updateActions();
}

QString EventDetailDialog::eventId() const { return event_.eventId; }

void EventDetailDialog::rebuild()
{
    summaryLabel_->setText(
        tr("事件 %1\n资源：%2\n说明：%3\n首次：%4　最近：%5　次数：%6　Revision：%7")
            .arg(event_.eventId,
                 event_.displayNameSnapshot.isEmpty()
                    ? event_.localResourceId : event_.displayNameSnapshot,
                 event_.note.isEmpty() ? tr("—") : event_.note,
                 localTime(event_.openedAtUtc), localTime(event_.lastObservedAtUtc))
            .arg(event_.occurrenceCount).arg(event_.eventRevision));
    historyTable_->setRowCount(0);
    for (const auto &entry : event_.history) {
        const int row = historyTable_->rowCount();
        historyTable_->insertRow(row);
        historyTable_->setItem(row, 0, new QTableWidgetItem(localTime(entry.atUtc)));
        historyTable_->setItem(row, 1, new QTableWidgetItem(eventTransitionKindName(entry.transition)));
        historyTable_->setItem(row, 2, new QTableWidgetItem(entry.source));
        historyTable_->setItem(row, 3, new QTableWidgetItem(entry.note));
    }
    controlTable_->setRowCount(0);
    for (const auto &attempt : event_.linkedControlAttempts) {
        const int row = controlTable_->rowCount();
        controlTable_->insertRow(row);
        controlTable_->setItem(row, 0, new QTableWidgetItem(localTime(attempt.observedAtUtc)));
        controlTable_->setItem(row, 1, new QTableWidgetItem(attempt.action));
        controlTable_->setItem(row, 2, new QTableWidgetItem(attempt.localOutcome));
        controlTable_->setItem(row, 3, new QTableWidgetItem(
            tr("不可用（不代表设备已执行）")));
    }
    evidenceTable_->setRowCount(0);
    for (const auto &record : records_) {
        const int row = evidenceTable_->rowCount();
        evidenceTable_->insertRow(row);
        evidenceTable_->setItem(row, 0, new QTableWidgetItem(record.evidenceId));
        evidenceTable_->setItem(row, 1, new QTableWidgetItem(localTime(record.capturedAtUtc)));
        evidenceTable_->setItem(row, 2, new QTableWidgetItem(QString::number(record.streamId)));
        evidenceTable_->setItem(row, 3, new QTableWidgetItem(QString::number(record.frameFreshnessMs)));
        evidenceTable_->setItem(row, 4, new QTableWidgetItem(QString::number(record.sizeBytes)));
        evidenceTable_->setItem(row, 5, new QTableWidgetItem(availabilityText(record.availability)));
    }
    attemptTable_->setRowCount(0);
    for (const auto &attempt : attempts_) {
        const int row = attemptTable_->rowCount();
        attemptTable_->insertRow(row);
        attemptTable_->setItem(row, 0, new QTableWidgetItem(localTime(attempt.requestedAtUtc)));
        attemptTable_->setItem(row, 1, new QTableWidgetItem(QString::number(attempt.streamId)));
        attemptTable_->setItem(row, 2, new QTableWidgetItem(QString::number(attempt.frameFreshnessMs)));
        attemptTable_->setItem(row, 3, new QTableWidgetItem(failureText(attempt.failure)));
    }
    updateActions();
}

void EventDetailDialog::updateActions()
{
    captureButton_->setEnabled(
        eventWritable_ && evidenceWritable_ &&
        event_.state != SecurityEventState::Closed &&
        sourceCombo_->currentIndex() >= 0);
    exportButton_->setEnabled(evidenceWritable_ && !event_.eventId.isEmpty());
}

void EventDetailDialog::chooseExportDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择事件导出目标父目录"));
    if (!directory.isEmpty()) emit exportRequested(event_.eventId, directory);
}
