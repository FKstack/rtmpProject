#include "app/EvidenceCoordinator.h"

#include <QJsonArray>
#include <QJsonObject>

#include <utility>

#include "app/PlatformEventBridge.h"
#include "app/StreamConnectionController.h"
#include "event_center/EventCenterService.h"
#include "evidence/EvidenceService.h"
#include "logging/LogManager.h"
#include "ui/MainWindow.h"

namespace {

QString dateText(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs)
                           : QString();
}

SecurityEventSeverity faultSeverity(EvidenceFaultKind kind)
{
    return kind == EvidenceFaultKind::ObjectWrite
        ? SecurityEventSeverity::Medium : SecurityEventSeverity::High;
}

} // namespace

EvidenceCoordinator::EvidenceCoordinator(
    EvidenceService *evidenceService,
    EventCenterService *eventCenterService,
    StreamConnectionController *connectionController,
    MainWindow *mainWindow,
    LogManager *logManager,
    QObject *parent)
    : QObject(parent), evidenceService_(evidenceService),
      eventCenterService_(eventCenterService),
      connectionController_(connectionController), mainWindow_(mainWindow),
      logManager_(logManager)
{
    Q_ASSERT(evidenceService_ != nullptr);
    Q_ASSERT(eventCenterService_ != nullptr);
    Q_ASSERT(connectionController_ != nullptr);
    Q_ASSERT(mainWindow_ != nullptr);
    Q_ASSERT(logManager_ != nullptr);
    connect(evidenceService_, &EvidenceService::catalogChanged,
            this, [this] { synchronizeProjection(); });
    connect(evidenceService_, &EvidenceService::subsystemFaultObserved,
            this, &EvidenceCoordinator::observeFault);
    connect(evidenceService_, &EvidenceService::subsystemRecoveryObserved,
            this, &EvidenceCoordinator::observeRecovery);
    connect(evidenceService_, &EvidenceService::captureCompleted,
            this, [this](const EvidenceRecord &record, const QImage &) {
                EvidenceCaptureAttempt attempt;
                const auto attempts = evidenceService_->attemptsForEvent(record.eventId);
                if (!attempts.isEmpty()) attempt = attempts.back();
                auditCapture(attempt, record.evidenceId, record.sizeBytes, true);
                emit operationMessage(tr("截图证据已保存"),
                    tr("本地截图证据已原子保存并关联到事件；未进行内容哈希校验。"), false);
            });
    connect(evidenceService_, &EvidenceService::captureFailed,
            this, [this](const EvidenceCaptureAttempt &attempt,
                         const QString &message) {
                auditCapture(attempt, {}, 0, false);
                emit operationMessage(tr("截图证据未保存"), message, true);
            });
    connect(evidenceService_, &EvidenceService::exportCompleted,
            this, [this](const EvidenceExportResult &result) {
                AuditRecord audit;
                audit.timestampUtc = QDateTime::currentDateTimeUtc();
                audit.actor = PlatformEventBridge::localActorName();
                audit.action = AuditAction::ExportIncident;
                audit.targetType = QStringLiteral("event");
                audit.targetId = result.eventId;
                audit.result = result.succeeded ? AuditResult::Success
                                                : AuditResult::Failure;
                audit.reason = result.message;
                audit.source = QStringLiteral("event-center");
                audit.afterValues = {
                    {QStringLiteral("exportId"), result.exportId},
                    {QStringLiteral("contentHashVerification"),
                     QStringLiteral("not_performed")},
                };
                logManager_->logAudit(audit);
                emit operationMessage(
                    result.succeeded ? tr("事件目录已导出")
                                     : tr("事件目录导出失败"),
                    result.succeeded
                        ? tr("事件资料目录已生成；未进行内容哈希、签名或可信时间戳校验。")
                        : result.message,
                    !result.succeeded);
            });
}

QList<EventResourceDescriptor> EvidenceCoordinator::captureResources(
    const QList<EventResourceDescriptor> &allResources) const
{
    QList<EventResourceDescriptor> result;
    for (const auto &resource : allResources) {
        if (connectionController_->streamIdForEventResourceId(
                resource.localResourceId) != kInvalidStreamId) {
            result.append(resource);
        }
    }
    return result;
}

void EvidenceCoordinator::setResources(
    const QList<EventResourceDescriptor> &resources)
{
    resources_ = resources;
}

std::optional<SecurityEventRecord> EvidenceCoordinator::findEvent(
    const QString &eventId) const
{
    for (const auto &event : eventCenterService_->events()) {
        if (event.eventId == eventId)
            return event;
    }
    return std::nullopt;
}

void EvidenceCoordinator::capture(const QString &eventId,
                                  const QString &sourceResourceId,
                                  const QString &actor)
{
    const auto event = findEvent(eventId);
    if (!event.has_value()) {
        emit operationMessage(tr("截图证据未保存"), tr("事件不存在。"), true);
        return;
    }
    if (event->state == SecurityEventState::Closed) {
        emit operationMessage(tr("截图证据未保存"),
                              tr("已关闭事件不能追加新证据。"), true);
        return;
    }
    const StreamId streamId = connectionController_->streamIdForEventResourceId(
        sourceResourceId);
    if (streamId == kInvalidStreamId) {
        emit operationMessage(tr("截图证据未保存"),
                              tr("选择的视频资源已不存在。"), true);
        return;
    }
    const ControlMediaObservation observation =
        connectionController_->controlMediaObservation(streamId);
    EvidenceCaptureRequest request;
    request.eventId = eventId;
    request.streamId = streamId;
    request.requestedAtUtc = QDateTime::currentDateTimeUtc();
    request.capturedAtUtc = request.requestedAtUtc;
    request.frameFreshnessMs = observation.presentedFrameAgeMs;
    request.playbackPlaying = observation.playbackPlaying;
    request.sourcePlaybackState = observation.playbackPlaying
        ? QStringLiteral("playing") : QStringLiteral("not-playing");
    request.image = mainWindow_->capturePresentedVideoFrame(streamId);
    request.actor = actor;
    for (const auto &resource : resources_) {
        if (resource.localResourceId == sourceResourceId) {
            request.deviceId = resource.deviceId;
            request.identitySource = resource.identitySource;
            break;
        }
    }
    const EvidenceOperationResult result = evidenceService_->capture(
        std::move(request));
    if (!result.accepted && result.message.isEmpty())
        emit operationMessage(tr("截图证据未保存"),
                              tr("证据采集请求被拒绝。"), true);
}

IncidentExportRequest EvidenceCoordinator::exportRequest(
    const SecurityEventRecord &event,
    const QString &destinationParentDirectory,
    const QString &actor) const
{
    IncidentExportRequest request;
    request.eventId = event.eventId;
    request.eventRevision = event.eventRevision;
    request.destinationParentDirectory = destinationParentDirectory;
    request.actor = actor;
    request.eventSnapshot = {
        {QStringLiteral("eventId"), event.eventId},
        {QStringLiteral("eventType"), securityEventTypeName(event.eventType)},
        {QStringLiteral("severity"), securityEventSeverityName(event.severity)},
        {QStringLiteral("state"), securityEventStateName(event.state)},
        {QStringLiteral("localResourceId"), event.localResourceId},
        {QStringLiteral("deviceId"), event.deviceId},
        {QStringLiteral("displayNameSnapshot"), event.displayNameSnapshot},
        {QStringLiteral("openedAtUtc"), dateText(event.openedAtUtc)},
        {QStringLiteral("lastObservedAtUtc"), dateText(event.lastObservedAtUtc)},
        {QStringLiteral("closedAtUtc"), dateText(event.closedAtUtc)},
        {QStringLiteral("occurrenceCount"), static_cast<double>(event.occurrenceCount)},
        {QStringLiteral("eventRevision"), static_cast<double>(event.eventRevision)},
        {QStringLiteral("note"), event.note},
    };
    for (const auto &history : event.history) {
        request.stateHistory.append(QJsonObject {
            {QStringLiteral("revision"), static_cast<double>(history.revision)},
            {QStringLiteral("transition"), eventTransitionKindName(history.transition)},
            {QStringLiteral("atUtc"), dateText(history.atUtc)},
            {QStringLiteral("source"), history.source},
            {QStringLiteral("actor"), history.actor},
            {QStringLiteral("actorAssurance"), history.actorAssurance},
            {QStringLiteral("note"), history.note},
        });
    }
    for (const auto &attempt : event.linkedControlAttempts) {
        request.linkedControlAttempts.append(QJsonObject {
            {QStringLiteral("attemptId"), attempt.attemptId},
            {QStringLiteral("observedAtUtc"), dateText(attempt.observedAtUtc)},
            {QStringLiteral("action"), attempt.action},
            {QStringLiteral("localOutcome"), attempt.localOutcome},
            {QStringLiteral("source"), attempt.source},
            {QStringLiteral("executionConfirmation"), QStringLiteral("unavailable")},
            {QStringLiteral("targetDeviceId"), attempt.targetDeviceId},
            {QStringLiteral("identitySource"), attempt.identitySource},
        });
    }
    return request;
}

void EvidenceCoordinator::exportIncident(
    const QString &eventId, const QString &destinationParentDirectory,
    const QString &actor)
{
    const auto event = findEvent(eventId);
    if (!event.has_value()) {
        emit operationMessage(tr("事件目录导出失败"), tr("事件不存在。"), true);
        return;
    }
    const EvidenceOperationResult result = evidenceService_->exportIncident(
        exportRequest(*event, destinationParentDirectory, actor));
    if (!result.accepted)
        emit operationMessage(tr("事件目录导出失败"), result.message, true);
}

void EvidenceCoordinator::synchronizeProjection()
{
    if (!evidenceService_->isInitialized() ||
        !evidenceService_->isWriteEnabled()) {
        return;
    }
    const EventOperationResult result = eventCenterService_->replaceEvidenceProjection(
        evidenceService_->evidenceProjection());
    if (!result.succeeded()) {
        logManager_->logSystem(
            LogLevel::Critical, QStringLiteral("evidence"),
            QStringLiteral("event_projection_failed"), result.message,
            {{QStringLiteral("playbackAffected"), false},
             {QStringLiteral("controlAffected"), false}});
    }
}

void EvidenceCoordinator::reportInitialStorageState()
{
    if (!evidenceService_->isWriteEnabled()) {
        observeFault(EvidenceFaultKind::Catalog,
                     evidenceService_->storageError());
        return;
    }
    bool inconsistent = false;
    for (const auto &record : evidenceService_->records()) {
        inconsistent = inconsistent ||
            record.availability != EvidenceAvailability::Available;
    }
    if (inconsistent) {
        observeFault(EvidenceFaultKind::Consistency,
                     tr("证据目录存在缺失文件或不安全路径。"));
    }
}

void EvidenceCoordinator::observeFault(EvidenceFaultKind kind,
                                       const QString &message)
{
    EventObservation observation;
    observation.eventType = SecurityEventType::LocalEvidenceSubsystemFault;
    observation.severity = faultSeverity(kind);
    observation.localResourceId = evidenceFaultResourceId(kind);
    observation.displayNameSnapshot = tr("本地证据子系统");
    observation.identitySource = QStringLiteral("local");
    observation.source = QStringLiteral("evidence-service");
    observation.note = message;
    const EventOperationResult result = eventCenterService_->observeFault(observation);
    if (!result.succeeded())
        qWarning().noquote() << result.message;
}

void EvidenceCoordinator::observeRecovery(EvidenceFaultKind kind)
{
    EventObservation observation;
    observation.eventType = SecurityEventType::LocalEvidenceSubsystemFault;
    observation.localResourceId = evidenceFaultResourceId(kind);
    observation.displayNameSnapshot = tr("本地证据子系统");
    observation.identitySource = QStringLiteral("local");
    observation.source = QStringLiteral("evidence-service");
    const EventOperationResult result = eventCenterService_->observeRecovery(observation);
    if (!result.succeeded() && result.error != EventOperationError::NotFound)
        qWarning().noquote() << result.message;
}

void EvidenceCoordinator::auditCapture(const EvidenceCaptureAttempt &attempt,
                                       const QString &evidenceId,
                                       qint64 sizeBytes,
                                       bool succeeded)
{
    AuditRecord audit;
    audit.timestampUtc = QDateTime::currentDateTimeUtc();
    audit.actor = attempt.actor;
    audit.action = AuditAction::CaptureEvidence;
    audit.targetType = QStringLiteral("event");
    audit.targetId = attempt.eventId;
    audit.result = succeeded ? AuditResult::Success : AuditResult::Failure;
    audit.reason = evidenceCaptureFailureName(attempt.failure);
    audit.source = QStringLiteral("event-center");
    audit.afterValues = {
        {QStringLiteral("attemptId"), attempt.attemptId},
        {QStringLiteral("evidenceId"), evidenceId},
        {QStringLiteral("streamId"), static_cast<double>(attempt.streamId)},
        {QStringLiteral("sizeBytes"), static_cast<double>(sizeBytes)},
        {QStringLiteral("contentHashVerification"), QStringLiteral("not_performed")},
    };
    logManager_->logAudit(audit);
}
