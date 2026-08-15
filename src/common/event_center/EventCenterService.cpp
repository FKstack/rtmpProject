#include "event_center/EventCenterService.h"

#include <QThread>
#include <QUuid>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {

constexpr int kMaximumClosedEvents = 5'000;
constexpr int kClosedRetentionDays = 180;

EventOperationResult failure(EventOperationError error, const QString &message)
{
    return {false, {}, error, message};
}

} // namespace

EventCenterService::EventCenterService(QString storagePath, Clock clock,
                                       QObject *parent)
    : QObject(parent)
    , store_(std::move(storagePath))
    , clock_(std::move(clock))
{
    qRegisterMetaType<SecurityEventRecord>();
    qRegisterMetaType<EventCenterSummary>();
    qRegisterMetaType<EventOperationResult>();
}

bool EventCenterService::initialize(QString *error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (initialized_) {
        if (error) *error = storageError_;
        return writeEnabled_;
    }
    initialized_ = true;
    const EventStoreLoadResult loaded = store_.load();
    if (!loaded.ok) {
        writeEnabled_ = false;
        storageError_ = loaded.error;
        if (error) *error = storageError_;
        emit storageStateChanged(false, storageError_);
        return false;
    }
    QString validationError;
    if (!validateLoadedState(loaded.events, loaded.tombstones,
                             &validationError)) {
        writeEnabled_ = false;
        storageError_ = QStringLiteral(
            "平台事件文件语义损坏，已保留原文件：%1").arg(validationError);
        if (error) *error = storageError_;
        emit storageStateChanged(false, storageError_);
        return false;
    }
    events_ = loaded.events;
    tombstones_ = loaded.tombstones;
    writeEnabled_ = !loaded.writeBlocked;
    storageError_ = loaded.error;
    if (error) *error = storageError_;
    emit storageStateChanged(writeEnabled_, storageError_);
    emit eventsChanged(events_, summary());
    return writeEnabled_;
}

void EventCenterService::stopAccepting()
{
    Q_ASSERT(QThread::currentThread() == thread());
    accepting_ = false;
}

bool EventCenterService::isInitialized() const noexcept { return initialized_; }
bool EventCenterService::isWriteEnabled() const noexcept { return writeEnabled_; }
QString EventCenterService::storageError() const { return storageError_; }
QString EventCenterService::storagePath() const { return store_.filePath(); }
QList<SecurityEventRecord> EventCenterService::events() const { return events_; }
QList<SecurityEventTombstone> EventCenterService::tombstones() const { return tombstones_; }

EventCenterSummary EventCenterService::summary() const
{
    EventCenterSummary result;
    bool hasActive = false;
    for (const auto &event : events_) {
        if (event.state == SecurityEventState::Closed) continue;
        ++result.activeCount;
        if (!hasActive || static_cast<int>(event.severity) >
                              static_cast<int>(result.highestSeverity)) {
            result.highestSeverity = event.severity;
            hasActive = true;
        }
    }
    return result;
}

QDateTime EventCenterService::nowUtc() const
{
    return (clock_ ? clock_() : QDateTime::currentDateTimeUtc()).toUTC();
}

bool EventCenterService::validateLoadedState(
    const QList<SecurityEventRecord> &events,
    const QList<SecurityEventTombstone> &tombstones,
    QString *error)
{
    QSet<QString> eventIds;
    QSet<QString> activeKeys;
    for (const auto &event : events) {
        if (eventIds.contains(event.eventId)) {
            if (error) *error = QStringLiteral("事件 ID 重复：%1").arg(event.eventId);
            return false;
        }
        eventIds.insert(event.eventId);
        if (event.state != SecurityEventState::Closed) {
            const QString key = securityEventTypeName(event.eventType) +
                                QLatin1Char('\n') + event.localResourceId;
            if (activeKeys.contains(key)) {
                if (error) *error = QStringLiteral("活动事件键重复。");
                return false;
            }
            activeKeys.insert(key);
        }
        if (event.state == SecurityEventState::Acknowledged &&
            !event.acknowledgedAtUtc.isValid()) {
            if (error) *error = QStringLiteral("Acknowledged 事件缺少确认时间。");
            return false;
        }
        if (event.state == SecurityEventState::Resolved &&
            (!event.resolvedAtUtc.isValid() ||
             event.resolutionSource == ResolutionSource::None)) {
            if (error) *error = QStringLiteral("Resolved 事件缺少恢复来源或时间。");
            return false;
        }
        if (event.state == SecurityEventState::Closed &&
            (!event.closedAtUtc.isValid() ||
             event.closeDisposition == CloseDisposition::None)) {
            if (error) *error = QStringLiteral("Closed 事件缺少关闭语义。");
            return false;
        }
    }
    for (const auto &tombstone : tombstones) {
        if (eventIds.contains(tombstone.eventId)) {
            if (error) *error = QStringLiteral("事件与 tombstone ID 重复。");
            return false;
        }
        eventIds.insert(tombstone.eventId);
    }
    return true;
}

EventOperationResult EventCenterService::availabilityError() const
{
    if (!initialized_)
        return failure(EventOperationError::NotInitialized,
                       QStringLiteral("平台事件中心尚未初始化。"));
    if (!accepting_)
        return failure(EventOperationError::Stopped,
                       QStringLiteral("平台事件中心已停止接收操作。"));
    if (!writeEnabled_)
        return failure(EventOperationError::StorageUnavailable,
                       storageError_.isEmpty()
                           ? QStringLiteral("平台事件存储不可写。")
                           : storageError_);
    return {};
}

int EventCenterService::findActive(SecurityEventType type,
                                   const QString &resourceId) const
{
    for (int index = 0; index < events_.size(); ++index) {
        const auto &event = events_.at(index);
        if (event.eventType == type && event.localResourceId == resourceId &&
            event.state != SecurityEventState::Closed) {
            return index;
        }
    }
    return -1;
}

int EventCenterService::findById(const QString &eventId) const
{
    for (int index = 0; index < events_.size(); ++index) {
        if (events_.at(index).eventId == eventId) return index;
    }
    return -1;
}

void EventCenterService::appendAttempt(
    SecurityEventRecord *event,
    const std::optional<EventControlAttemptSummary> &attempt)
{
    if (!attempt.has_value() || attempt->attemptId.trimmed().isEmpty()) return;
    const auto duplicate = std::find_if(
        event->linkedControlAttempts.cbegin(),
        event->linkedControlAttempts.cend(),
        [&](const EventControlAttemptSummary &existing) {
            return existing.attemptId == attempt->attemptId;
        });
    if (duplicate == event->linkedControlAttempts.cend())
        event->linkedControlAttempts.append(*attempt);
}

void EventCenterService::appendHistory(
    SecurityEventRecord *event, EventTransitionKind transition,
    std::optional<SecurityEventState> fromState, SecurityEventState toState,
    const QDateTime &atUtc, const QString &source, const QString &actor,
    const QString &note)
{
    event->history.append({
        event->eventRevision,
        transition,
        fromState,
        toState,
        atUtc,
        source,
        actor,
        QStringLiteral("unverified-local"),
        note,
    });
}

EventOperationResult EventCenterService::observeFault(
    const EventObservation &observation)
{
    const EventOperationResult unavailable = availabilityError();
    if (!unavailable.succeeded()) return unavailable;
    const QString resourceId = observation.localResourceId.trimmed();
    if (resourceId.isEmpty() ||
        observation.eventType == SecurityEventType::ManualIncident) {
        return failure(EventOperationError::InvalidInput,
                       QStringLiteral("系统事件类型或本地资源无效。"));
    }
    const QDateTime now = nowUtc();
    QList<SecurityEventRecord> candidate = events_;
    const int existingIndex = findActive(observation.eventType, resourceId);
    QString eventId;
    if (existingIndex < 0) {
        SecurityEventRecord event;
        event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        event.eventType = observation.eventType;
        event.severity = observation.severity;
        event.state = SecurityEventState::Open;
        event.localResourceId = resourceId;
        event.deviceId = observation.deviceId.trimmed();
        event.displayNameSnapshot = observation.displayNameSnapshot.trimmed();
        event.openedAtUtc = now;
        event.lastObservedAtUtc = now;
        event.identitySource = observation.identitySource.trimmed().isEmpty()
            ? QStringLiteral("url-derived") : observation.identitySource.trimmed();
        event.note = observation.note.trimmed();
        appendAttempt(&event, observation.controlAttempt);
        appendHistory(&event, EventTransitionKind::Created, std::nullopt,
                      SecurityEventState::Open, now,
                      observation.source.trimmed(), {});
        eventId = event.eventId;
        candidate.append(event);
    } else {
        SecurityEventRecord &event = candidate[existingIndex];
        eventId = event.eventId;
        ++event.eventRevision;
        ++event.occurrenceCount;
        event.lastObservedAtUtc = now;
        if (static_cast<int>(observation.severity) >
            static_cast<int>(event.severity)) {
            event.severity = observation.severity;
        }
        appendAttempt(&event, observation.controlAttempt);
        if (event.state == SecurityEventState::Resolved) {
            const SecurityEventState previous = event.state;
            event.state = SecurityEventState::Open;
            event.acknowledgedAtUtc = {};
            event.resolvedAtUtc = {};
            event.resolutionSource = ResolutionSource::None;
            event.closeDisposition = CloseDisposition::None;
            appendHistory(&event, EventTransitionKind::Recurred, previous,
                          event.state, now, observation.source.trimmed(), {});
        }
    }
    return commitOne(std::move(candidate), eventId, true);
}

EventOperationResult EventCenterService::observeRecovery(
    const EventObservation &observation)
{
    const EventOperationResult unavailable = availabilityError();
    if (!unavailable.succeeded()) return unavailable;
    if (observation.eventType == SecurityEventType::ManualIncident ||
        observation.localResourceId.trimmed().isEmpty()) {
        return failure(EventOperationError::InvalidInput,
                       QStringLiteral("恢复观察的事件类型或资源无效。"));
    }
    const int index = findActive(observation.eventType,
                                 observation.localResourceId.trimmed());
    if (index < 0) return {false, {}, EventOperationError::None, {}};
    const SecurityEventRecord &current = events_.at(index);
    if (current.state == SecurityEventState::Resolved)
        return {false, current.eventId, EventOperationError::None, {}};
    if (current.state != SecurityEventState::Open &&
        current.state != SecurityEventState::Acknowledged) {
        return failure(EventOperationError::IllegalTransition,
                       QStringLiteral("当前事件状态不能接受恢复观察。"));
    }
    QList<SecurityEventRecord> candidate = events_;
    SecurityEventRecord &event = candidate[index];
    const SecurityEventState previous = event.state;
    ++event.eventRevision;
    event.state = SecurityEventState::Resolved;
    event.lastObservedAtUtc = nowUtc();
    event.resolvedAtUtc = event.lastObservedAtUtc;
    event.resolutionSource = ResolutionSource::PlatformObservation;
    appendAttempt(&event, observation.controlAttempt);
    appendHistory(&event, EventTransitionKind::Recovered, previous, event.state,
                  event.resolvedAtUtc, observation.source.trimmed(), {});
    const QString committedId = event.eventId;
    return commitOne(std::move(candidate), committedId, true);
}

EventOperationResult EventCenterService::createManualIncident(
    SecurityEventSeverity severity, const QString &localResourceId,
    const QString &deviceId, const QString &displayNameSnapshot,
    const QString &identitySource, const QString &note, const QString &actor)
{
    const EventOperationResult unavailable = availabilityError();
    if (!unavailable.succeeded()) return unavailable;
    const QString resource = localResourceId.trimmed();
    const QString description = note.trimmed();
    const QString operatorName = actor.trimmed();
    if (resource.isEmpty() || description.isEmpty() || operatorName.isEmpty()) {
        return failure(EventOperationError::InvalidInput,
                       QStringLiteral("人工事件必须包含资源、说明和操作者。"));
    }
    const int existing = findActive(SecurityEventType::ManualIncident, resource);
    if (existing >= 0) {
        return failure(EventOperationError::IllegalTransition,
                       QStringLiteral("该资源已有活动中的人工事件。"));
    }
    const QDateTime now = nowUtc();
    SecurityEventRecord event;
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.eventType = SecurityEventType::ManualIncident;
    event.severity = severity;
    event.localResourceId = resource;
    event.deviceId = deviceId.trimmed();
    event.displayNameSnapshot = displayNameSnapshot.trimmed();
    event.openedAtUtc = now;
    event.lastObservedAtUtc = now;
    event.actor = operatorName;
    event.identitySource = identitySource.trimmed().isEmpty()
        ? QStringLiteral("local") : identitySource.trimmed();
    event.note = description;
    appendHistory(&event, EventTransitionKind::Created, std::nullopt,
                  event.state, now, QStringLiteral("operator"), operatorName,
                  description);
    QList<SecurityEventRecord> candidate = events_;
    candidate.append(event);
    const QString committedId = event.eventId;
    return commitOne(std::move(candidate), committedId, true);
}

EventOperationResult EventCenterService::acknowledge(const QString &eventId,
                                                      const QString &actor)
{
    const EventOperationResult unavailable = availabilityError();
    if (!unavailable.succeeded()) return unavailable;
    const int index = findById(eventId.trimmed());
    if (index < 0) return failure(EventOperationError::NotFound,
                                  QStringLiteral("事件不存在。"));
    if (events_.at(index).state != SecurityEventState::Open)
        return failure(EventOperationError::IllegalTransition,
                       QStringLiteral("只有 Open 事件可以确认。"));
    if (actor.trimmed().isEmpty())
        return failure(EventOperationError::InvalidInput,
                       QStringLiteral("操作者不能为空。"));
    QList<SecurityEventRecord> candidate = events_;
    SecurityEventRecord &event = candidate[index];
    const QDateTime now = nowUtc();
    ++event.eventRevision;
    event.state = SecurityEventState::Acknowledged;
    event.acknowledgedAtUtc = now;
    event.actor = actor.trimmed();
    appendHistory(&event, EventTransitionKind::Acknowledged,
                  SecurityEventState::Open, event.state, now,
                  QStringLiteral("operator"), event.actor);
    const QString committedId = event.eventId;
    return commitOne(std::move(candidate), committedId, true);
}

EventOperationResult EventCenterService::resolveManualIncident(
    const QString &eventId, const QString &actor)
{
    const EventOperationResult unavailable = availabilityError();
    if (!unavailable.succeeded()) return unavailable;
    const int index = findById(eventId.trimmed());
    if (index < 0) return failure(EventOperationError::NotFound,
                                  QStringLiteral("事件不存在。"));
    const auto &current = events_.at(index);
    if (current.eventType != SecurityEventType::ManualIncident ||
        current.state != SecurityEventState::Acknowledged) {
        return failure(EventOperationError::IllegalTransition,
                       QStringLiteral("只有已确认的人工事件可以由操作者解决。"));
    }
    if (actor.trimmed().isEmpty())
        return failure(EventOperationError::InvalidInput,
                       QStringLiteral("操作者不能为空。"));
    QList<SecurityEventRecord> candidate = events_;
    SecurityEventRecord &event = candidate[index];
    const QDateTime now = nowUtc();
    ++event.eventRevision;
    event.state = SecurityEventState::Resolved;
    event.resolvedAtUtc = now;
    event.resolutionSource = ResolutionSource::Operator;
    event.actor = actor.trimmed();
    appendHistory(&event, EventTransitionKind::ResolvedByOperator,
                  SecurityEventState::Acknowledged, event.state, now,
                  QStringLiteral("operator"), event.actor);
    const QString committedId = event.eventId;
    return commitOne(std::move(candidate), committedId, true);
}

EventOperationResult EventCenterService::closeResolved(const QString &eventId,
                                                        const QString &actor)
{
    const EventOperationResult unavailable = availabilityError();
    if (!unavailable.succeeded()) return unavailable;
    const int index = findById(eventId.trimmed());
    if (index < 0) return failure(EventOperationError::NotFound,
                                  QStringLiteral("事件不存在。"));
    if (events_.at(index).state != SecurityEventState::Resolved)
        return failure(EventOperationError::IllegalTransition,
                       QStringLiteral("只有 Resolved 事件可以关闭。"));
    if (actor.trimmed().isEmpty())
        return failure(EventOperationError::InvalidInput,
                       QStringLiteral("操作者不能为空。"));
    QList<SecurityEventRecord> candidate = events_;
    SecurityEventRecord &event = candidate[index];
    const QDateTime now = nowUtc();
    ++event.eventRevision;
    event.state = SecurityEventState::Closed;
    event.closedAtUtc = now;
    event.closeDisposition = CloseDisposition::ObservedRecovery;
    event.actor = actor.trimmed();
    appendHistory(&event, EventTransitionKind::Closed,
                  SecurityEventState::Resolved, event.state, now,
                  QStringLiteral("operator"), event.actor);
    const QString committedId = event.eventId;
    return commitOne(std::move(candidate), committedId, true);
}

EventOperationResult EventCenterService::closeWithoutObservedRecovery(
    const QString &eventId, const QString &reason, const QString &actor)
{
    const EventOperationResult unavailable = availabilityError();
    if (!unavailable.succeeded()) return unavailable;
    const int index = findById(eventId.trimmed());
    if (index < 0) return failure(EventOperationError::NotFound,
                                  QStringLiteral("事件不存在。"));
    const auto &current = events_.at(index);
    if (!isSystemEvent(current.eventType) ||
        (current.state != SecurityEventState::Open &&
         current.state != SecurityEventState::Acknowledged)) {
        return failure(EventOperationError::IllegalTransition,
                       QStringLiteral("该事件不能在未观察到恢复时强制关闭。"));
    }
    if (reason.trimmed().isEmpty() || actor.trimmed().isEmpty())
        return failure(EventOperationError::InvalidInput,
                       QStringLiteral("强制关闭必须填写原因和操作者。"));
    QList<SecurityEventRecord> candidate = events_;
    SecurityEventRecord &event = candidate[index];
    const SecurityEventState previous = event.state;
    const QDateTime now = nowUtc();
    ++event.eventRevision;
    event.state = SecurityEventState::Closed;
    event.closedAtUtc = now;
    event.resolvedAtUtc = {};
    event.resolutionSource = ResolutionSource::None;
    event.closeDisposition = CloseDisposition::ClosedWithoutObservedRecovery;
    event.actor = actor.trimmed();
    appendHistory(&event, EventTransitionKind::ClosedWithoutObservedRecovery,
                  previous, event.state, now, QStringLiteral("operator"),
                  event.actor, reason.trimmed());
    const QString committedId = event.eventId;
    return commitOne(std::move(candidate), committedId, true);
}

EventOperationResult EventCenterService::replaceEvidenceProjection(
    const QHash<QString, QStringList> &projection)
{
    const EventOperationResult unavailable = availabilityError();
    if (!unavailable.succeeded()) return unavailable;
    QList<SecurityEventRecord> candidateEvents = events_;
    QList<SecurityEventTombstone> candidateTombstones = tombstones_;
    bool changed = false;
    for (auto &event : candidateEvents) {
        QStringList desired = projection.value(event.eventId);
        desired.removeDuplicates();
        desired.sort();
        QStringList current = event.evidenceIds;
        current.removeDuplicates();
        current.sort();
        if (desired == current) continue;
        event.evidenceIds = desired;
        ++event.eventRevision;
        changed = true;
    }
    for (auto &tombstone : candidateTombstones) {
        QStringList desired = projection.value(tombstone.eventId);
        desired.removeDuplicates();
        desired.sort();
        QStringList current = tombstone.evidenceIds;
        current.removeDuplicates();
        current.sort();
        if (desired == current) continue;
        tombstone.evidenceIds = desired;
        ++tombstone.eventRevision;
        changed = true;
    }
    if (!changed) return {false, {}, EventOperationError::None, {}};
    QString error;
    if (!commit(std::move(candidateEvents), std::move(candidateTombstones),
                &error)) {
        return failure(EventOperationError::StorageUnavailable, error);
    }
    return {true, {}, EventOperationError::None, {}};
}

EventOperationResult EventCenterService::commitOne(
    QList<SecurityEventRecord> candidate, const QString &eventId, bool changed)
{
    QString error;
    if (!commit(std::move(candidate), tombstones_, &error))
        return failure(EventOperationError::StorageUnavailable, error);
    return {changed, eventId, EventOperationError::None, {}};
}

bool EventCenterService::commit(
    QList<SecurityEventRecord> candidateEvents,
    QList<SecurityEventTombstone> candidateTombstones,
    QString *error)
{
    const QDateTime now = nowUtc();
    applyRetention(&candidateEvents, &candidateTombstones, now);
    QString saveError;
    if (!store_.save(candidateEvents, candidateTombstones, now, &saveError)) {
        writeEnabled_ = false;
        storageError_ = saveError;
        if (error) *error = saveError;
        emit storageStateChanged(false, storageError_);
        emit operationFailed(storageError_);
        return false;
    }
    events_ = std::move(candidateEvents);
    tombstones_ = std::move(candidateTombstones);
    emit eventsChanged(events_, summary());
    return true;
}

void EventCenterService::applyRetention(
    QList<SecurityEventRecord> *events,
    QList<SecurityEventTombstone> *tombstones,
    const QDateTime &now) const
{
    QList<int> closedIndexes;
    for (int index = 0; index < events->size(); ++index) {
        if (events->at(index).state == SecurityEventState::Closed)
            closedIndexes.append(index);
    }
    std::sort(closedIndexes.begin(), closedIndexes.end(),
              [&](int left, int right) {
                  return events->at(left).closedAtUtc > events->at(right).closedAtUtc;
              });
    QList<int> removeIndexes;
    const QDateTime cutoff = now.addDays(-kClosedRetentionDays);
    for (int rank = 0; rank < closedIndexes.size(); ++rank) {
        const int index = closedIndexes.at(rank);
        const SecurityEventRecord &event = events->at(index);
        if (rank < kMaximumClosedEvents && event.closedAtUtc >= cutoff) continue;
        if (!event.evidenceIds.isEmpty()) {
            const bool exists = std::any_of(
                tombstones->cbegin(), tombstones->cend(),
                [&](const SecurityEventTombstone &item) {
                    return item.eventId == event.eventId;
                });
            if (!exists) {
                tombstones->append({
                    event.eventId, event.eventType, event.localResourceId,
                    event.deviceId, event.openedAtUtc, event.closedAtUtc,
                    event.closeDisposition, event.evidenceIds,
                    event.eventRevision,
                });
            }
        }
        removeIndexes.append(index);
    }
    std::sort(removeIndexes.begin(), removeIndexes.end(), std::greater<int>());
    for (int index : removeIndexes) events->removeAt(index);
}
