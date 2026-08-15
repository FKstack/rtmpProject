#include "evidence/EvidenceCatalogStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <optional>
#include <utility>

namespace {

QString dateText(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs)
                           : QString();
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

bool readDate(const QJsonObject &object, const char *name, bool required,
              QDateTime *value, QString *error)
{
    QString text;
    if (!readString(object, name, required, &text, error)) return false;
    if (text.isEmpty() && !required) {
        *value = {};
        return true;
    }
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid()) parsed = QDateTime::fromString(text, Qt::ISODate);
    if (!parsed.isValid()) {
        if (error) *error = QStringLiteral("字段 %1 不是有效时间。")
                                .arg(QLatin1String(name));
        return false;
    }
    *value = parsed.toUTC();
    return true;
}

bool readInteger(const QJsonObject &object, const char *name, qint64 minimum,
                 qint64 *value, QString *error)
{
    const QJsonValue raw = object.value(QLatin1String(name));
    const double number = raw.toDouble(static_cast<double>(minimum - 1));
    if (!raw.isDouble() || number < static_cast<double>(minimum) ||
        std::floor(number) != number) {
        if (error) *error = QStringLiteral("字段 %1 必须是整数。")
                                .arg(QLatin1String(name));
        return false;
    }
    *value = static_cast<qint64>(number);
    return true;
}

std::optional<EvidenceAvailability> availabilityFromName(const QString &name)
{
    if (name == QStringLiteral("available")) return EvidenceAvailability::Available;
    if (name == QStringLiteral("missing")) return EvidenceAvailability::Missing;
    if (name == QStringLiteral("unsafe_path")) return EvidenceAvailability::UnsafePath;
    return std::nullopt;
}

std::optional<EvidenceCaptureFailure> failureFromName(const QString &name)
{
    for (int raw = static_cast<int>(EvidenceCaptureFailure::None);
         raw <= static_cast<int>(EvidenceCaptureFailure::Stopped); ++raw) {
        const auto value = static_cast<EvidenceCaptureFailure>(raw);
        if (evidenceCaptureFailureName(value) == name) return value;
    }
    return std::nullopt;
}

bool recordFromJson(const QJsonValue &value, EvidenceRecord *record,
                    QString *error)
{
    if (!value.isObject()) {
        if (error) *error = QStringLiteral("证据记录必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    qint64 streamId = 0;
    qint64 sizeBytes = 0;
    QString availability;
    if (!readString(object, "evidenceId", true, &record->evidenceId, error) ||
        !readString(object, "eventId", true, &record->eventId, error) ||
        !readString(object, "type", true, &record->type, error) ||
        !readString(object, "deviceId", false, &record->deviceId, error) ||
        !readInteger(object, "streamId", 0, &streamId, error) ||
        !readString(object, "identitySource", true, &record->identitySource, error) ||
        !readDate(object, "captureRequestedAtUtc", true, &record->captureRequestedAtUtc, error) ||
        !readDate(object, "capturedAtUtc", true, &record->capturedAtUtc, error) ||
        !readDate(object, "writtenAtUtc", true, &record->writtenAtUtc, error) ||
        !readInteger(object, "frameFreshnessMs", -1, &record->frameFreshnessMs, error) ||
        !readString(object, "sourcePlaybackState", true, &record->sourcePlaybackState, error) ||
        !readString(object, "relativePath", true, &record->relativePath, error) ||
        !readInteger(object, "sizeBytes", 0, &sizeBytes, error) ||
        !readString(object, "actor", true, &record->actor, error) ||
        !readString(object, "actorAssurance", true, &record->actorAssurance, error) ||
        !readString(object, "availability", true, &availability, error) ||
        !readDate(object, "lastVerifiedAtUtc", true, &record->lastVerifiedAtUtc, error)) {
        return false;
    }
    const auto parsedAvailability = availabilityFromName(availability);
    if (!parsedAvailability) {
        if (error) *error = QStringLiteral("证据记录包含未知可用状态。");
        return false;
    }
    record->streamId = static_cast<quint64>(streamId);
    record->sizeBytes = sizeBytes;
    record->availability = *parsedAvailability;
    return true;
}

bool attemptFromJson(const QJsonValue &value, EvidenceCaptureAttempt *attempt,
                     QString *error)
{
    if (!value.isObject()) {
        if (error) *error = QStringLiteral("截图尝试必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    qint64 streamId = 0;
    QString failure;
    if (!readString(object, "attemptId", true, &attempt->attemptId, error) ||
        !readString(object, "eventId", true, &attempt->eventId, error) ||
        !readString(object, "evidenceId", false, &attempt->evidenceId, error) ||
        !readString(object, "deviceId", false, &attempt->deviceId, error) ||
        !readInteger(object, "streamId", 0, &streamId, error) ||
        !readDate(object, "requestedAtUtc", true, &attempt->requestedAtUtc, error) ||
        !readDate(object, "completedAtUtc", true, &attempt->completedAtUtc, error) ||
        !readInteger(object, "frameFreshnessMs", -1, &attempt->frameFreshnessMs, error) ||
        !readString(object, "sourcePlaybackState", true, &attempt->sourcePlaybackState, error) ||
        !readString(object, "failure", true, &failure, error) ||
        !readString(object, "actor", true, &attempt->actor, error) ||
        !readString(object, "actorAssurance", true, &attempt->actorAssurance, error)) {
        return false;
    }
    if (!object.value(QStringLiteral("succeeded")).isBool()) {
        if (error) *error = QStringLiteral("字段 succeeded 必须是布尔值。");
        return false;
    }
    const auto parsedFailure = failureFromName(failure);
    if (!parsedFailure) {
        if (error) *error = QStringLiteral("截图尝试包含未知失败原因。");
        return false;
    }
    attempt->streamId = static_cast<quint64>(streamId);
    attempt->succeeded = object.value(QStringLiteral("succeeded")).toBool();
    attempt->failure = *parsedFailure;
    return true;
}

QJsonObject auditToJson(const EvidenceExportAudit &audit)
{
    return {
        {QStringLiteral("exportId"), audit.exportId},
        {QStringLiteral("eventId"), audit.eventId},
        {QStringLiteral("eventRevision"), static_cast<double>(audit.eventRevision)},
        {QStringLiteral("exportedAtUtc"), dateText(audit.exportedAtUtc)},
        {QStringLiteral("actor"), audit.actor},
        {QStringLiteral("actorAssurance"), audit.actorAssurance},
        {QStringLiteral("succeeded"), audit.succeeded},
        {QStringLiteral("outputDirectoryName"), audit.outputDirectoryName},
        {QStringLiteral("failureReason"), audit.failureReason},
    };
}

bool auditFromJson(const QJsonValue &value, EvidenceExportAudit *audit,
                   QString *error)
{
    if (!value.isObject()) {
        if (error) *error = QStringLiteral("导出审计必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    qint64 revision = 0;
    if (!readString(object, "exportId", true, &audit->exportId, error) ||
        !readString(object, "eventId", true, &audit->eventId, error) ||
        !readInteger(object, "eventRevision", 0, &revision, error) ||
        !readDate(object, "exportedAtUtc", true, &audit->exportedAtUtc, error) ||
        !readString(object, "actor", true, &audit->actor, error) ||
        !readString(object, "actorAssurance", true, &audit->actorAssurance, error) ||
        !readString(object, "outputDirectoryName", false, &audit->outputDirectoryName, error) ||
        !readString(object, "failureReason", false, &audit->failureReason, error)) {
        return false;
    }
    if (!object.value(QStringLiteral("succeeded")).isBool()) {
        if (error) *error = QStringLiteral("导出审计 succeeded 必须是布尔值。");
        return false;
    }
    audit->eventRevision = static_cast<quint64>(revision);
    audit->succeeded = object.value(QStringLiteral("succeeded")).toBool();
    return true;
}

} // namespace

EvidenceCatalogStore::EvidenceCatalogStore(QString filePath)
    : filePath_(std::move(filePath))
{
}

EvidenceCatalogLoadResult EvidenceCatalogStore::load() const
{
    EvidenceCatalogLoadResult result;
    QFile file(filePath_);
    if (!file.exists()) {
        result.ok = true;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.writeBlocked = true;
        result.error = QStringLiteral("无法读取证据目录：%1").arg(file.errorString());
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.writeBlocked = true;
        result.error = QStringLiteral("证据目录损坏，已保留原文件：%1")
                           .arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (version > kSchemaVersion) {
        result.writeBlocked = true;
        result.error = QStringLiteral("证据目录版本 %1 高于当前支持版本 %2，已禁用写入。")
                           .arg(version).arg(kSchemaVersion);
        return result;
    }
    if (version != kSchemaVersion ||
        !root.value(QStringLiteral("records")).isArray() ||
        !root.value(QStringLiteral("captureAttempts")).isArray() ||
        !root.value(QStringLiteral("exportAudits")).isArray()) {
        result.writeBlocked = true;
        result.error = QStringLiteral("证据目录 schema v1 格式无效。");
        return result;
    }
    QString error;
    for (const QJsonValue &item : root.value(QStringLiteral("records")).toArray()) {
        EvidenceRecord record;
        if (!recordFromJson(item, &record, &error)) {
            result.writeBlocked = true;
            result.error = QStringLiteral("证据记录损坏，已保留原文件：%1").arg(error);
            return result;
        }
        result.records.append(record);
    }
    for (const QJsonValue &item : root.value(QStringLiteral("captureAttempts")).toArray()) {
        EvidenceCaptureAttempt attempt;
        if (!attemptFromJson(item, &attempt, &error)) {
            result.writeBlocked = true;
            result.error = QStringLiteral("截图尝试记录损坏，已保留原文件：%1").arg(error);
            return result;
        }
        result.captureAttempts.append(attempt);
    }
    for (const QJsonValue &item : root.value(QStringLiteral("exportAudits")).toArray()) {
        EvidenceExportAudit audit;
        if (!auditFromJson(item, &audit, &error)) {
            result.writeBlocked = true;
            result.error = QStringLiteral("导出审计损坏，已保留原文件：%1").arg(error);
            return result;
        }
        result.exportAudits.append(audit);
    }
    result.ok = true;
    return result;
}

bool EvidenceCatalogStore::save(
    const QList<EvidenceRecord> &records,
    const QList<EvidenceCaptureAttempt> &attempts,
    const QList<EvidenceExportAudit> &audits,
    const QDateTime &savedAtUtc,
    QString *error) const
{
    const QFileInfo info(filePath_);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QStringLiteral("无法创建证据目录。");
        return false;
    }
    QJsonArray recordArray;
    for (const auto &record : records) recordArray.append(evidenceRecordToJson(record));
    QJsonArray attemptArray;
    for (const auto &attempt : attempts)
        attemptArray.append(evidenceCaptureAttemptToJson(attempt));
    QJsonArray auditArray;
    for (const auto &audit : audits) auditArray.append(auditToJson(audit));
    const QJsonObject root {
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("savedAtUtc"), dateText(savedAtUtc)},
        {QStringLiteral("records"), recordArray},
        {QStringLiteral("captureAttempts"), attemptArray},
        {QStringLiteral("exportAudits"), auditArray},
    };
    QSaveFile output(filePath_);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入证据目录：%1").arg(output.errorString());
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(data) != data.size() || !output.commit()) {
        if (error) *error = QStringLiteral("无法原子提交证据目录。");
        return false;
    }
    return true;
}

QString EvidenceCatalogStore::filePath() const
{
    return filePath_;
}
