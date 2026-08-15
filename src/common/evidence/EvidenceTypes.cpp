#include "evidence/EvidenceTypes.h"

namespace {

QString dateText(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs)
                           : QString();
}

} // namespace

QString evidenceAvailabilityName(EvidenceAvailability value)
{
    switch (value) {
    case EvidenceAvailability::Available: return QStringLiteral("available");
    case EvidenceAvailability::Missing: return QStringLiteral("missing");
    case EvidenceAvailability::UnsafePath: return QStringLiteral("unsafe_path");
    }
    return QStringLiteral("missing");
}

QString evidenceCaptureFailureName(EvidenceCaptureFailure value)
{
    switch (value) {
    case EvidenceCaptureFailure::None: return QStringLiteral("none");
    case EvidenceCaptureFailure::StorageUnavailable: return QStringLiteral("storage_unavailable");
    case EvidenceCaptureFailure::StorageLow: return QStringLiteral("storage_low");
    case EvidenceCaptureFailure::QueueFull: return QStringLiteral("queue_full");
    case EvidenceCaptureFailure::PlaybackNotPlaying: return QStringLiteral("playback_not_playing");
    case EvidenceCaptureFailure::FrameUnavailable: return QStringLiteral("frame_unavailable");
    case EvidenceCaptureFailure::FrameStale: return QStringLiteral("frame_stale");
    case EvidenceCaptureFailure::EmptyImage: return QStringLiteral("empty_image");
    case EvidenceCaptureFailure::DirectoryCreateFailed: return QStringLiteral("directory_create_failed");
    case EvidenceCaptureFailure::ImageEncodeFailed: return QStringLiteral("image_encode_failed");
    case EvidenceCaptureFailure::FileCommitFailed: return QStringLiteral("file_commit_failed");
    case EvidenceCaptureFailure::CatalogCommitFailed: return QStringLiteral("catalog_commit_failed");
    case EvidenceCaptureFailure::InvalidInput: return QStringLiteral("invalid_input");
    case EvidenceCaptureFailure::Stopped: return QStringLiteral("stopped");
    }
    return QStringLiteral("storage_unavailable");
}

QString evidenceFaultResourceId(EvidenceFaultKind value)
{
    switch (value) {
    case EvidenceFaultKind::StorageCapacity: return QStringLiteral("evidence:storage-capacity");
    case EvidenceFaultKind::ObjectWrite: return QStringLiteral("evidence:object-write");
    case EvidenceFaultKind::Catalog: return QStringLiteral("evidence:catalog");
    case EvidenceFaultKind::Consistency: return QStringLiteral("evidence:consistency");
    }
    return QStringLiteral("evidence:catalog");
}

QJsonObject evidenceRecordToJson(const EvidenceRecord &record)
{
    return {
        {QStringLiteral("evidenceId"), record.evidenceId},
        {QStringLiteral("eventId"), record.eventId},
        {QStringLiteral("type"), record.type},
        {QStringLiteral("deviceId"), record.deviceId},
        {QStringLiteral("streamId"), static_cast<double>(record.streamId)},
        {QStringLiteral("identitySource"), record.identitySource},
        {QStringLiteral("captureRequestedAtUtc"), dateText(record.captureRequestedAtUtc)},
        {QStringLiteral("capturedAtUtc"), dateText(record.capturedAtUtc)},
        {QStringLiteral("writtenAtUtc"), dateText(record.writtenAtUtc)},
        {QStringLiteral("frameFreshnessMs"), record.frameFreshnessMs},
        {QStringLiteral("sourcePlaybackState"), record.sourcePlaybackState},
        {QStringLiteral("relativePath"), record.relativePath},
        {QStringLiteral("sizeBytes"), static_cast<double>(record.sizeBytes)},
        {QStringLiteral("actor"), record.actor},
        {QStringLiteral("actorAssurance"), record.actorAssurance},
        {QStringLiteral("availability"), evidenceAvailabilityName(record.availability)},
        {QStringLiteral("lastVerifiedAtUtc"), dateText(record.lastVerifiedAtUtc)},
    };
}

QJsonObject evidenceCaptureAttemptToJson(const EvidenceCaptureAttempt &attempt)
{
    return {
        {QStringLiteral("attemptId"), attempt.attemptId},
        {QStringLiteral("eventId"), attempt.eventId},
        {QStringLiteral("evidenceId"), attempt.evidenceId},
        {QStringLiteral("deviceId"), attempt.deviceId},
        {QStringLiteral("streamId"), static_cast<double>(attempt.streamId)},
        {QStringLiteral("requestedAtUtc"), dateText(attempt.requestedAtUtc)},
        {QStringLiteral("completedAtUtc"), dateText(attempt.completedAtUtc)},
        {QStringLiteral("frameFreshnessMs"), attempt.frameFreshnessMs},
        {QStringLiteral("sourcePlaybackState"), attempt.sourcePlaybackState},
        {QStringLiteral("succeeded"), attempt.succeeded},
        {QStringLiteral("failure"), evidenceCaptureFailureName(attempt.failure)},
        {QStringLiteral("actor"), attempt.actor},
        {QStringLiteral("actorAssurance"), attempt.actorAssurance},
    };
}
