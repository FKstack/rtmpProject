#include "event_center/EventCenterStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>
#include <utility>

namespace {

QString dateText(const QDateTime &value)
{
    return value.isValid()
        ? value.toUTC().toString(Qt::ISODateWithMs)
        : QString();
}

bool readDate(const QJsonObject &object, const char *name, bool required,
              QDateTime *value, QString *error)
{
    const QJsonValue raw = object.value(QLatin1String(name));
    if (!raw.isString()) {
        if (!required && raw.isUndefined()) {
            *value = {};
            return true;
        }
        if (error) *error = QStringLiteral("字段 %1 必须是时间字符串。")
                                .arg(QLatin1String(name));
        return false;
    }
    const QString text = raw.toString();
    if (text.isEmpty() && !required) {
        *value = {};
        return true;
    }
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid()) parsed = QDateTime::fromString(text, Qt::ISODate);
    if (!parsed.isValid()) {
        if (error) *error = QStringLiteral("字段 %1 不是有效 UTC 时间。")
                                .arg(QLatin1String(name));
        return false;
    }
    *value = parsed.toUTC();
    return true;
}

bool readString(const QJsonObject &object, const char *name, bool required,
                QString *value, QString *error)
{
    const QJsonValue raw = object.value(QLatin1String(name));
    if (!raw.isString()) {
        if (!required && raw.isUndefined()) {
            value->clear();
            return true;
        }
        if (error) *error = QStringLiteral("字段 %1 必须是字符串。")
                                .arg(QLatin1String(name));
        return false;
    }
    *value = raw.toString();
    if (required && value->isEmpty()) {
        if (error) *error = QStringLiteral("字段 %1 不能为空。")
                                .arg(QLatin1String(name));
        return false;
    }
    return true;
}

bool readCounter(const QJsonObject &object, const char *name, quint64 *value,
                 QString *error)
{
    const QJsonValue raw = object.value(QLatin1String(name));
    const double number = raw.toDouble(-1.0);
    if (!raw.isDouble() || number < 1.0 || std::floor(number) != number) {
        if (error) *error = QStringLiteral("字段 %1 必须是正整数。")
                                .arg(QLatin1String(name));
        return false;
    }
    *value = static_cast<quint64>(number);
    return true;
}

QJsonObject controlAttemptToJson(const EventControlAttemptSummary &attempt)
{
    return {
        {QStringLiteral("attemptId"), attempt.attemptId},
        {QStringLiteral("observedAtUtc"), dateText(attempt.observedAtUtc)},
        {QStringLiteral("action"), attempt.action},
        {QStringLiteral("localOutcome"), attempt.localOutcome},
        {QStringLiteral("source"), attempt.source},
        {QStringLiteral("executionConfirmation"), attempt.executionConfirmation},
        {QStringLiteral("targetDeviceId"), attempt.targetDeviceId},
        {QStringLiteral("identitySource"), attempt.identitySource},
    };
}

bool controlAttemptFromJson(const QJsonValue &value,
                            EventControlAttemptSummary *attempt,
                            QString *error)
{
    if (!value.isObject()) {
        if (error) *error = QStringLiteral("控制尝试摘要必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    return readString(object, "attemptId", true, &attempt->attemptId, error) &&
           readDate(object, "observedAtUtc", true, &attempt->observedAtUtc, error) &&
           readString(object, "action", true, &attempt->action, error) &&
           readString(object, "localOutcome", true, &attempt->localOutcome, error) &&
           readString(object, "source", true, &attempt->source, error) &&
           readString(object, "executionConfirmation", true,
                      &attempt->executionConfirmation, error) &&
           readString(object, "targetDeviceId", false,
                      &attempt->targetDeviceId, error) &&
           readString(object, "identitySource", true,
                      &attempt->identitySource, error);
}

QJsonObject historyToJson(const SecurityEventHistoryEntry &entry)
{
    return {
        {QStringLiteral("revision"), static_cast<double>(entry.revision)},
        {QStringLiteral("transition"), eventTransitionKindName(entry.transition)},
        {QStringLiteral("fromState"), entry.fromState.has_value()
             ? securityEventStateName(*entry.fromState) : QString()},
        {QStringLiteral("toState"), securityEventStateName(entry.toState)},
        {QStringLiteral("atUtc"), dateText(entry.atUtc)},
        {QStringLiteral("source"), entry.source},
        {QStringLiteral("actor"), entry.actor},
        {QStringLiteral("actorAssurance"), entry.actorAssurance},
        {QStringLiteral("note"), entry.note},
    };
}

bool historyFromJson(const QJsonValue &value, SecurityEventHistoryEntry *entry,
                     QString *error)
{
    if (!value.isObject()) {
        if (error) *error = QStringLiteral("事件历史必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    QString transition;
    QString fromState;
    QString toState;
    if (!readCounter(object, "revision", &entry->revision, error) ||
        !readString(object, "transition", true, &transition, error) ||
        !readString(object, "fromState", false, &fromState, error) ||
        !readString(object, "toState", true, &toState, error) ||
        !readDate(object, "atUtc", true, &entry->atUtc, error) ||
        !readString(object, "source", false, &entry->source, error) ||
        !readString(object, "actor", false, &entry->actor, error) ||
        !readString(object, "actorAssurance", true,
                    &entry->actorAssurance, error) ||
        !readString(object, "note", false, &entry->note, error)) {
        return false;
    }
    const auto parsedTransition = eventTransitionKindFromName(transition);
    const auto parsedToState = securityEventStateFromName(toState);
    if (!parsedTransition || !parsedToState) {
        if (error) *error = QStringLiteral("事件历史包含未知枚举值。");
        return false;
    }
    entry->transition = *parsedTransition;
    entry->toState = *parsedToState;
    if (!fromState.isEmpty()) {
        const auto parsedFromState = securityEventStateFromName(fromState);
        if (!parsedFromState) {
            if (error) *error = QStringLiteral("事件历史包含未知起始状态。");
            return false;
        }
        entry->fromState = *parsedFromState;
    }
    return true;
}

QJsonArray stringListToJson(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values) result.append(value);
    return result;
}

bool stringListFromJson(const QJsonValue &value, QStringList *result,
                        QString *error)
{
    if (!value.isArray()) {
        if (error) *error = QStringLiteral("字符串列表字段必须是数组。");
        return false;
    }
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isString()) {
            if (error) *error = QStringLiteral("字符串列表包含非字符串值。");
            return false;
        }
        result->append(item.toString());
    }
    return true;
}

QJsonObject eventToJson(const SecurityEventRecord &event)
{
    QJsonArray attempts;
    for (const auto &attempt : event.linkedControlAttempts)
        attempts.append(controlAttemptToJson(attempt));
    QJsonArray history;
    for (const auto &entry : event.history) history.append(historyToJson(entry));
    return {
        {QStringLiteral("eventId"), event.eventId},
        {QStringLiteral("eventType"), securityEventTypeName(event.eventType)},
        {QStringLiteral("severity"), securityEventSeverityName(event.severity)},
        {QStringLiteral("state"), securityEventStateName(event.state)},
        {QStringLiteral("localResourceId"), event.localResourceId},
        {QStringLiteral("deviceId"), event.deviceId},
        {QStringLiteral("displayNameSnapshot"), event.displayNameSnapshot},
        {QStringLiteral("openedAtUtc"), dateText(event.openedAtUtc)},
        {QStringLiteral("lastObservedAtUtc"), dateText(event.lastObservedAtUtc)},
        {QStringLiteral("acknowledgedAtUtc"), dateText(event.acknowledgedAtUtc)},
        {QStringLiteral("resolvedAtUtc"), dateText(event.resolvedAtUtc)},
        {QStringLiteral("closedAtUtc"), dateText(event.closedAtUtc)},
        {QStringLiteral("resolutionSource"), resolutionSourceName(event.resolutionSource)},
        {QStringLiteral("closeDisposition"), closeDispositionName(event.closeDisposition)},
        {QStringLiteral("eventRevision"), static_cast<double>(event.eventRevision)},
        {QStringLiteral("occurrenceCount"), static_cast<double>(event.occurrenceCount)},
        {QStringLiteral("actor"), event.actor},
        {QStringLiteral("actorAssurance"), event.actorAssurance},
        {QStringLiteral("identitySource"), event.identitySource},
        {QStringLiteral("note"), event.note},
        {QStringLiteral("linkedControlAttempts"), attempts},
        {QStringLiteral("evidenceIds"), stringListToJson(event.evidenceIds)},
        {QStringLiteral("history"), history},
    };
}

bool eventFromJson(const QJsonValue &value, SecurityEventRecord *event,
                   QString *error)
{
    if (!value.isObject()) {
        if (error) *error = QStringLiteral("事件记录必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    QString type;
    QString severity;
    QString state;
    QString resolution;
    QString disposition;
    if (!readString(object, "eventId", true, &event->eventId, error) ||
        !readString(object, "eventType", true, &type, error) ||
        !readString(object, "severity", true, &severity, error) ||
        !readString(object, "state", true, &state, error) ||
        !readString(object, "localResourceId", true,
                    &event->localResourceId, error) ||
        !readString(object, "deviceId", false, &event->deviceId, error) ||
        !readString(object, "displayNameSnapshot", false,
                    &event->displayNameSnapshot, error) ||
        !readDate(object, "openedAtUtc", true, &event->openedAtUtc, error) ||
        !readDate(object, "lastObservedAtUtc", true,
                  &event->lastObservedAtUtc, error) ||
        !readDate(object, "acknowledgedAtUtc", false,
                  &event->acknowledgedAtUtc, error) ||
        !readDate(object, "resolvedAtUtc", false, &event->resolvedAtUtc, error) ||
        !readDate(object, "closedAtUtc", false, &event->closedAtUtc, error) ||
        !readString(object, "resolutionSource", false, &resolution, error) ||
        !readString(object, "closeDisposition", false, &disposition, error) ||
        !readCounter(object, "eventRevision", &event->eventRevision, error) ||
        !readCounter(object, "occurrenceCount", &event->occurrenceCount, error) ||
        !readString(object, "actor", false, &event->actor, error) ||
        !readString(object, "actorAssurance", true,
                    &event->actorAssurance, error) ||
        !readString(object, "identitySource", true,
                    &event->identitySource, error) ||
        !readString(object, "note", false, &event->note, error)) {
        return false;
    }
    const auto parsedType = securityEventTypeFromName(type);
    const auto parsedSeverity = securityEventSeverityFromName(severity);
    const auto parsedState = securityEventStateFromName(state);
    const auto parsedResolution = resolutionSourceFromName(resolution);
    const auto parsedDisposition = closeDispositionFromName(disposition);
    if (!parsedType || !parsedSeverity || !parsedState || !parsedResolution ||
        !parsedDisposition) {
        if (error) *error = QStringLiteral("事件记录包含未知枚举值。");
        return false;
    }
    event->eventType = *parsedType;
    event->severity = *parsedSeverity;
    event->state = *parsedState;
    event->resolutionSource = *parsedResolution;
    event->closeDisposition = *parsedDisposition;

    const QJsonValue attempts = object.value(QStringLiteral("linkedControlAttempts"));
    const QJsonValue evidence = object.value(QStringLiteral("evidenceIds"));
    const QJsonValue history = object.value(QStringLiteral("history"));
    if (!attempts.isArray() || !history.isArray() ||
        !stringListFromJson(evidence, &event->evidenceIds, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("事件数组字段格式无效。");
        return false;
    }
    for (const QJsonValue &item : attempts.toArray()) {
        EventControlAttemptSummary attempt;
        if (!controlAttemptFromJson(item, &attempt, error)) return false;
        event->linkedControlAttempts.append(attempt);
    }
    for (const QJsonValue &item : history.toArray()) {
        SecurityEventHistoryEntry entry;
        if (!historyFromJson(item, &entry, error)) return false;
        event->history.append(entry);
    }
    return true;
}

QJsonObject tombstoneToJson(const SecurityEventTombstone &tombstone)
{
    return {
        {QStringLiteral("eventId"), tombstone.eventId},
        {QStringLiteral("eventType"), securityEventTypeName(tombstone.eventType)},
        {QStringLiteral("localResourceId"), tombstone.localResourceId},
        {QStringLiteral("deviceId"), tombstone.deviceId},
        {QStringLiteral("openedAtUtc"), dateText(tombstone.openedAtUtc)},
        {QStringLiteral("closedAtUtc"), dateText(tombstone.closedAtUtc)},
        {QStringLiteral("closeDisposition"), closeDispositionName(tombstone.closeDisposition)},
        {QStringLiteral("evidenceIds"), stringListToJson(tombstone.evidenceIds)},
        {QStringLiteral("eventRevision"), static_cast<double>(tombstone.eventRevision)},
    };
}

bool tombstoneFromJson(const QJsonValue &value, SecurityEventTombstone *result,
                       QString *error)
{
    if (!value.isObject()) {
        if (error) *error = QStringLiteral("事件 tombstone 必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    QString type;
    QString disposition;
    if (!readString(object, "eventId", true, &result->eventId, error) ||
        !readString(object, "eventType", true, &type, error) ||
        !readString(object, "localResourceId", true,
                    &result->localResourceId, error) ||
        !readString(object, "deviceId", false, &result->deviceId, error) ||
        !readDate(object, "openedAtUtc", true, &result->openedAtUtc, error) ||
        !readDate(object, "closedAtUtc", true, &result->closedAtUtc, error) ||
        !readString(object, "closeDisposition", false, &disposition, error) ||
        !stringListFromJson(object.value(QStringLiteral("evidenceIds")),
                            &result->evidenceIds, error) ||
        !readCounter(object, "eventRevision", &result->eventRevision, error)) {
        return false;
    }
    const auto parsedType = securityEventTypeFromName(type);
    const auto parsedDisposition = closeDispositionFromName(disposition);
    if (!parsedType || !parsedDisposition) {
        if (error) *error = QStringLiteral("事件 tombstone 包含未知枚举值。");
        return false;
    }
    result->eventType = *parsedType;
    result->closeDisposition = *parsedDisposition;
    return true;
}

} // namespace

EventCenterStore::EventCenterStore(QString filePath)
    : filePath_(filePath.trimmed().isEmpty() ? defaultFilePath()
                                             : std::move(filePath))
{
}

EventStoreLoadResult EventCenterStore::load() const
{
    EventStoreLoadResult result;
    QFile file(filePath_);
    if (!file.exists()) {
        result.ok = true;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.writeBlocked = true;
        result.error = QStringLiteral("无法读取平台事件文件：%1").arg(file.errorString());
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.writeBlocked = true;
        result.error = QStringLiteral("平台事件文件损坏，已保留原文件：%1")
                           .arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = document.object();
    const QJsonValue versionValue = root.value(QStringLiteral("schemaVersion"));
    if (!versionValue.isDouble()) {
        result.writeBlocked = true;
        result.error = QStringLiteral("平台事件文件缺少有效 schemaVersion。");
        return result;
    }
    const int version = versionValue.toInt(-1);
    if (version > kSchemaVersion) {
        result.writeBlocked = true;
        result.error = QStringLiteral("平台事件文件版本 %1 高于当前支持的版本 %2，已禁用写入。")
                           .arg(version).arg(kSchemaVersion);
        return result;
    }
    if ((version != 1 && version != kSchemaVersion) ||
        !root.value(QStringLiteral("events")).isArray() ||
        !root.value(QStringLiteral("tombstones")).isArray()) {
        result.writeBlocked = true;
        result.error = QStringLiteral("平台事件文件 schema 格式无效。");
        return result;
    }
    QString recordError;
    for (const QJsonValue &item : root.value(QStringLiteral("events")).toArray()) {
        SecurityEventRecord event;
        if (!eventFromJson(item, &event, &recordError)) {
            result.writeBlocked = true;
            result.error = QStringLiteral("平台事件文件记录损坏，已保留原文件：%1")
                               .arg(recordError);
            return result;
        }
        result.events.append(event);
    }
    for (const QJsonValue &item : root.value(QStringLiteral("tombstones")).toArray()) {
        SecurityEventTombstone tombstone;
        if (!tombstoneFromJson(item, &tombstone, &recordError)) {
            result.writeBlocked = true;
            result.error = QStringLiteral("平台事件 tombstone 损坏，已保留原文件：%1")
                               .arg(recordError);
            return result;
        }
        result.tombstones.append(tombstone);
    }
    result.ok = true;
    return result;
}

bool EventCenterStore::save(const QList<SecurityEventRecord> &events,
                            const QList<SecurityEventTombstone> &tombstones,
                            const QDateTime &savedAtUtc, QString *error) const
{
    const QFileInfo info(filePath_);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QStringLiteral("无法创建平台事件目录。");
        return false;
    }
    QJsonArray eventArray;
    for (const auto &event : events) eventArray.append(eventToJson(event));
    QJsonArray tombstoneArray;
    for (const auto &tombstone : tombstones)
        tombstoneArray.append(tombstoneToJson(tombstone));
    const QJsonObject root {
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("savedAtUtc"), dateText(savedAtUtc)},
        {QStringLiteral("events"), eventArray},
        {QStringLiteral("tombstones"), tombstoneArray},
    };
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入平台事件文件：%1")
                                .arg(file.errorString());
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        if (error) *error = QStringLiteral("平台事件文件写入不完整：%1")
                                .arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = QStringLiteral("平台事件文件原子提交失败：%1")
                                .arg(file.errorString());
        return false;
    }
    return true;
}

QString EventCenterStore::filePath() const
{
    return filePath_;
}

QString EventCenterStore::defaultFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/incidents/events-v1.json");
}
