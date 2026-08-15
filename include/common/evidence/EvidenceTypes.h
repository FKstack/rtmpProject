#pragma once

#include <QDateTime>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

enum class EvidenceAvailability { Available, Missing, UnsafePath };

enum class EvidenceCaptureFailure {
    None,
    StorageUnavailable,
    StorageLow,
    QueueFull,
    PlaybackNotPlaying,
    FrameUnavailable,
    FrameStale,
    EmptyImage,
    DirectoryCreateFailed,
    ImageEncodeFailed,
    FileCommitFailed,
    CatalogCommitFailed,
    InvalidInput,
    Stopped,
};

enum class EvidenceFaultKind {
    StorageCapacity,
    ObjectWrite,
    Catalog,
    Consistency,
};

struct EvidenceRecord
{
    QString evidenceId;
    QString eventId;
    QString type = QStringLiteral("Screenshot");
    QString deviceId;
    quint64 streamId = 0;
    QString identitySource = QStringLiteral("url-derived");
    QDateTime captureRequestedAtUtc;
    QDateTime capturedAtUtc;
    QDateTime writtenAtUtc;
    qint64 frameFreshnessMs = -1;
    QString sourcePlaybackState;
    QString relativePath;
    qint64 sizeBytes = 0;
    QString actor;
    QString actorAssurance = QStringLiteral("unverified-local");
    EvidenceAvailability availability = EvidenceAvailability::Available;
    QDateTime lastVerifiedAtUtc;
};

struct EvidenceCaptureAttempt
{
    QString attemptId;
    QString eventId;
    QString evidenceId;
    QString deviceId;
    quint64 streamId = 0;
    QDateTime requestedAtUtc;
    QDateTime completedAtUtc;
    qint64 frameFreshnessMs = -1;
    QString sourcePlaybackState;
    bool succeeded = false;
    EvidenceCaptureFailure failure = EvidenceCaptureFailure::None;
    QString actor;
    QString actorAssurance = QStringLiteral("unverified-local");
};

struct EvidenceExportAudit
{
    QString exportId;
    QString eventId;
    quint64 eventRevision = 0;
    QDateTime exportedAtUtc;
    QString actor;
    QString actorAssurance = QStringLiteral("unverified-local");
    bool succeeded = false;
    QString outputDirectoryName;
    QString failureReason;
};

struct EvidenceCaptureRequest
{
    QString eventId;
    QString deviceId;
    quint64 streamId = 0;
    QString identitySource = QStringLiteral("url-derived");
    QDateTime requestedAtUtc;
    QDateTime capturedAtUtc;
    qint64 frameFreshnessMs = -1;
    QString sourcePlaybackState;
    bool playbackPlaying = false;
    QImage image;
    QString actor;
    QString actorAssurance = QStringLiteral("unverified-local");
};

struct IncidentExportRequest
{
    QString eventId;
    quint64 eventRevision = 0;
    QString destinationParentDirectory;
    QString actor;
    QString actorAssurance = QStringLiteral("unverified-local");
    QJsonObject eventSnapshot;
    QJsonArray stateHistory;
    QList<QJsonObject> linkedControlAttempts;
};

struct EvidenceOperationResult
{
    bool accepted = false;
    QString operationId;
    EvidenceCaptureFailure failure = EvidenceCaptureFailure::None;
    QString message;
};

struct EvidenceExportResult
{
    bool succeeded = false;
    QString exportId;
    QString eventId;
    QString outputDirectory;
    QString message;
};

[[nodiscard]] QString evidenceAvailabilityName(EvidenceAvailability value);
[[nodiscard]] QString evidenceCaptureFailureName(EvidenceCaptureFailure value);
[[nodiscard]] QString evidenceFaultResourceId(EvidenceFaultKind value);
[[nodiscard]] QJsonObject evidenceRecordToJson(const EvidenceRecord &record);
[[nodiscard]] QJsonObject evidenceCaptureAttemptToJson(
    const EvidenceCaptureAttempt &attempt);

Q_DECLARE_METATYPE(EvidenceRecord)
Q_DECLARE_METATYPE(EvidenceCaptureAttempt)
Q_DECLARE_METATYPE(EvidenceOperationResult)
Q_DECLARE_METATYPE(EvidenceExportResult)
Q_DECLARE_METATYPE(EvidenceFaultKind)
