#pragma once

#include <QWidget>

#include "event_center/EventCenterTypes.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

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

private:
    void rebuildTable();
    void updateActions();
    void createManualIncident();
    void forceCloseSelected();
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
    QList<SecurityEventRecord> events_;
    QList<EventResourceDescriptor> resources_;
    bool writeEnabled_ = true;
};
