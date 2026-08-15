#pragma once

#include <QDialog>

#include "event_center/EventCenterTypes.h"
#include "evidence/EvidenceTypes.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

/** Read-only incident details plus explicit capture/export intents. */
class EventDetailDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit EventDetailDialog(QWidget *parent = nullptr);

    void setEvent(const SecurityEventRecord &event);
    void setEvidence(const QList<EvidenceRecord> &records,
                     const QList<EvidenceCaptureAttempt> &attempts);
    void setCaptureResources(const QList<EventResourceDescriptor> &resources);
    void setStorageState(bool eventWritable, bool evidenceWritable,
                         const QString &evidenceError);
    [[nodiscard]] QString eventId() const;

signals:
    void captureRequested(const QString &eventId,
                          const QString &sourceResourceId);
    void exportRequested(const QString &eventId,
                         const QString &destinationParentDirectory);

private:
    void rebuild();
    void updateActions();
    void chooseExportDirectory();

    QLabel *summaryLabel_ = nullptr;
    QLabel *noticeLabel_ = nullptr;
    QLabel *storageBanner_ = nullptr;
    QTableWidget *historyTable_ = nullptr;
    QTableWidget *controlTable_ = nullptr;
    QTableWidget *evidenceTable_ = nullptr;
    QTableWidget *attemptTable_ = nullptr;
    QComboBox *sourceCombo_ = nullptr;
    QPushButton *captureButton_ = nullptr;
    QPushButton *exportButton_ = nullptr;
    SecurityEventRecord event_;
    QList<EvidenceRecord> records_;
    QList<EvidenceCaptureAttempt> attempts_;
    QList<EventResourceDescriptor> resources_;
    bool eventWritable_ = true;
    bool evidenceWritable_ = true;
};
