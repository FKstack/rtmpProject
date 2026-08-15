#pragma once

#include <QObject>

#include <functional>

#include "event_center/EventCenterStore.h"

/** Owns the event state machine and commits every accepted mutation atomically. */
class EventCenterService final : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;

    explicit EventCenterService(
        QString storagePath = {},
        Clock clock = {},
        QObject *parent = nullptr
    );

    [[nodiscard]] bool initialize(QString *error = nullptr);
    void stopAccepting();

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] bool isWriteEnabled() const noexcept;
    [[nodiscard]] QString storageError() const;
    [[nodiscard]] QString storagePath() const;
    [[nodiscard]] QList<SecurityEventRecord> events() const;
    [[nodiscard]] QList<SecurityEventTombstone> tombstones() const;
    [[nodiscard]] EventCenterSummary summary() const;

    EventOperationResult observeFault(const EventObservation &observation);
    EventOperationResult observeRecovery(const EventObservation &observation);
    EventOperationResult createManualIncident(
        SecurityEventSeverity severity,
        const QString &localResourceId,
        const QString &deviceId,
        const QString &displayNameSnapshot,
        const QString &identitySource,
        const QString &note,
        const QString &actor
    );
    EventOperationResult acknowledge(const QString &eventId,
                                     const QString &actor);
    EventOperationResult resolveManualIncident(const QString &eventId,
                                               const QString &actor);
    EventOperationResult closeResolved(const QString &eventId,
                                       const QString &actor);
    EventOperationResult closeWithoutObservedRecovery(
        const QString &eventId,
        const QString &reason,
        const QString &actor
    );

signals:
    void eventsChanged(const QList<SecurityEventRecord> &events,
                       const EventCenterSummary &summary);
    void storageStateChanged(bool writeEnabled, const QString &error);
    void operationFailed(const QString &message);

private:
    [[nodiscard]] QDateTime nowUtc() const;
    [[nodiscard]] static bool validateLoadedState(
        const QList<SecurityEventRecord> &events,
        const QList<SecurityEventTombstone> &tombstones,
        QString *error
    );
    [[nodiscard]] EventOperationResult availabilityError() const;
    [[nodiscard]] int findActive(SecurityEventType type,
                                 const QString &resourceId) const;
    [[nodiscard]] int findById(const QString &eventId) const;
    [[nodiscard]] bool commit(QList<SecurityEventRecord> candidateEvents,
                              QList<SecurityEventTombstone> candidateTombstones,
                              QString *error);
    void applyRetention(QList<SecurityEventRecord> *events,
                        QList<SecurityEventTombstone> *tombstones,
                        const QDateTime &now) const;
    static void appendAttempt(SecurityEventRecord *event,
                              const std::optional<EventControlAttemptSummary> &attempt);
    static void appendHistory(SecurityEventRecord *event,
                              EventTransitionKind transition,
                              std::optional<SecurityEventState> fromState,
                              SecurityEventState toState,
                              const QDateTime &atUtc,
                              const QString &source,
                              const QString &actor,
                              const QString &note = {});
    EventOperationResult commitOne(QList<SecurityEventRecord> candidate,
                                   const QString &eventId,
                                   bool changed);

    EventCenterStore store_;
    Clock clock_;
    QList<SecurityEventRecord> events_;
    QList<SecurityEventTombstone> tombstones_;
    QString storageError_;
    bool initialized_ = false;
    bool writeEnabled_ = false;
    bool accepting_ = true;
};
