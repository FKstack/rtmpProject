#pragma once

#include <QObject>

#include <optional>

#include "event_center/EventCenterTypes.h"
#include "evidence/EvidenceTypes.h"

class EventCenterService;
class EvidenceService;
class LogManager;
class MainWindow;
class StreamConnectionController;

/** Composition-layer coordinator; no lower module depends on evidence UI. */
class EvidenceCoordinator final : public QObject
{
    Q_OBJECT

public:
    EvidenceCoordinator(EvidenceService *evidenceService,
                        EventCenterService *eventCenterService,
                        StreamConnectionController *connectionController,
                        MainWindow *mainWindow,
                        LogManager *logManager,
                        QObject *parent = nullptr);

    [[nodiscard]] QList<EventResourceDescriptor> captureResources(
        const QList<EventResourceDescriptor> &allResources) const;
    void setResources(const QList<EventResourceDescriptor> &resources);
    void capture(const QString &eventId, const QString &sourceResourceId,
                 const QString &actor);
    void exportIncident(const QString &eventId,
                        const QString &destinationParentDirectory,
                        const QString &actor);
    void synchronizeProjection();
    void reportInitialStorageState();

signals:
    void operationMessage(const QString &title, const QString &message,
                          bool error);

private:
    [[nodiscard]] std::optional<SecurityEventRecord> findEvent(
        const QString &eventId) const;
    [[nodiscard]] IncidentExportRequest exportRequest(
        const SecurityEventRecord &event,
        const QString &destinationParentDirectory,
        const QString &actor) const;
    void observeFault(EvidenceFaultKind kind, const QString &message);
    void observeRecovery(EvidenceFaultKind kind);
    void auditCapture(const EvidenceCaptureAttempt &attempt,
                      const QString &evidenceId,
                      qint64 sizeBytes,
                      bool succeeded);

    EvidenceService *evidenceService_ = nullptr;
    EventCenterService *eventCenterService_ = nullptr;
    StreamConnectionController *connectionController_ = nullptr;
    MainWindow *mainWindow_ = nullptr;
    LogManager *logManager_ = nullptr;
    QList<EventResourceDescriptor> resources_;
};
