#pragma once

#include <QWidget>
#include <QPointer>

#include "event_center/EventCenterTypes.h"
#include "evidence/EvidenceTypes.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class EventDetailDialog;

/** Phase-2A list and lifecycle actions; it never opens the event store. */
class EventCenterPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit EventCenterPanel(QWidget *parent = nullptr);

    void setEvents(const QList<SecurityEventRecord> &events,
                   const EventCenterSummary &summary);
    void setResources(const QList<EventResourceDescriptor> &resources);
    void setStorageState(bool writeEnabled, const QString &error);
    void setEvidenceStorageState(bool writeEnabled, const QString &error);
    void setEvidenceData(const QList<EvidenceRecord> &records,
                         const QList<EvidenceCaptureAttempt> &attempts);
    void setCaptureResources(const QList<EventResourceDescriptor> &resources);
    void showOperationError(const QString &message);

signals:
    void manualIncidentRequested(
        SecurityEventSeverity severity,
        const QString &localResourceId,
        const QString &deviceId,
        const QString &displayName,
        const QString &identitySource,
        const QString &note
    );
    void acknowledgeRequested(const QString &eventId);
    void resolveManualRequested(const QString &eventId);
    void closeRequested(const QString &eventId);
    void forceCloseRequested(const QString &eventId, const QString &reason);
    void captureEvidenceRequested(const QString &eventId,
                                  const QString &sourceResourceId);
    void exportEventRequested(const QString &eventId,
                              const QString &destinationParentDirectory);

private:
    void rebuildTable();
    void updateActions();
    void createManualIncident();
    void forceCloseSelected();
    void openSelectedDetails();
    void refreshActiveDetail();
    [[nodiscard]] QString selectedEventId() const;
    [[nodiscard]] const SecurityEventRecord *selectedEvent() const;

    QLabel *storageBanner_ = nullptr;
    QComboBox *filterCombo_ = nullptr;
    QPushButton *createButton_ = nullptr;
    QTableWidget *table_ = nullptr;
    QPushButton *acknowledgeButton_ = nullptr;
    QPushButton *resolveButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QPushButton *forceCloseButton_ = nullptr;
    QPushButton *detailButton_ = nullptr;
    QList<SecurityEventRecord> events_;
    QList<EventResourceDescriptor> resources_;
    QList<EventResourceDescriptor> captureResources_;
    QList<EvidenceRecord> evidenceRecords_;
    QList<EvidenceCaptureAttempt> evidenceAttempts_;
    QPointer<EventDetailDialog> activeDetail_;
    QString evidenceStorageError_;
    bool writeEnabled_ = true;
    bool evidenceWriteEnabled_ = false;
};
