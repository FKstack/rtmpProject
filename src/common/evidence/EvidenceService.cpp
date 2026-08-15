#include "evidence/EvidenceService.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <utility>

#include "evidence/AtomicPngWriter.h"

namespace {

QString defaultEvidenceRoot()
{
    const QString appData = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return QDir(appData).filePath(QStringLiteral("incidents/evidence"));
}

QString catalogPath(const QString &root)
{
    return QDir(root).filePath(QStringLiteral("catalog-v1.json"));
}

QString operationMessage(EvidenceCaptureFailure failure)
{
    switch (failure) {
    case EvidenceCaptureFailure::None: return {};
    case EvidenceCaptureFailure::StorageUnavailable: return QStringLiteral("证据存储不可写。");
    case EvidenceCaptureFailure::StorageLow: return QStringLiteral("磁盘可用空间不足，已拒绝采集新证据。");
    case EvidenceCaptureFailure::QueueFull: return QStringLiteral("证据写入队列已满，请稍后重试。");
    case EvidenceCaptureFailure::PlaybackNotPlaying: return QStringLiteral("当前视频不是播放状态，未生成截图证据。");
    case EvidenceCaptureFailure::FrameUnavailable: return QStringLiteral("当前视频尚无已呈现画面，未生成截图证据。");
    case EvidenceCaptureFailure::FrameStale: return QStringLiteral("当前画面已过期，未生成截图证据。");
    case EvidenceCaptureFailure::EmptyImage: return QStringLiteral("当前渲染画面不可用，未生成截图证据。");
    case EvidenceCaptureFailure::DirectoryCreateFailed: return QStringLiteral("无法创建证据对象目录。");
    case EvidenceCaptureFailure::ImageEncodeFailed: return QStringLiteral("PNG 编码失败。");
    case EvidenceCaptureFailure::FileCommitFailed: return QStringLiteral("PNG 文件原子提交失败。");
    case EvidenceCaptureFailure::CatalogCommitFailed: return QStringLiteral("证据目录原子提交失败。");
    case EvidenceCaptureFailure::InvalidInput: return QStringLiteral("证据操作参数不完整。");
    case EvidenceCaptureFailure::Stopped: return QStringLiteral("证据服务已停止接收新任务。");
    }
    return QStringLiteral("证据操作失败。");
}

EvidenceCaptureFailure writeFailure(const QString &error)
{
    if (error.contains(QStringLiteral("目录")))
        return EvidenceCaptureFailure::DirectoryCreateFailed;
    if (error.contains(QStringLiteral("编码")))
        return EvidenceCaptureFailure::ImageEncodeFailed;
    return EvidenceCaptureFailure::FileCommitFailed;
}

bool copyFileAtomically(const QString &source, const QString &destination,
                        QString *error)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取证据文件。");
        return false;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法创建导出文件。");
        return false;
    }
    while (!input.atEnd()) {
        const QByteArray block = input.read(1024 * 1024);
        if (block.isEmpty() && input.error() != QFile::NoError) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("读取证据文件失败。");
            return false;
        }
        if (output.write(block) != block.size()) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("写入导出文件失败。");
            return false;
        }
    }
    if (!output.commit()) {
        if (error) *error = QStringLiteral("无法原子提交导出文件。");
        return false;
    }
    return true;
}

QString safeLeaf(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                  QStringLiteral("_"));
    return value.left(96);
}

} // namespace

EvidenceService::EvidenceService(QString rootPath, Clock clock,
                                 StorageProbe storageProbe,
                                 PngWriter pngWriter, QObject *parent)
    : QObject(parent),
      rootPath_(rootPath.trimmed().isEmpty() ? defaultEvidenceRoot()
                                             : QDir::cleanPath(rootPath)),
      store_(catalogPath(rootPath_)),
      clock_(clock ? std::move(clock) : [] { return QDateTime::currentDateTimeUtc(); }),
      storageProbe_(storageProbe ? std::move(storageProbe)
                                 : [](const QString &path) {
          const QStorageInfo storage(path);
          return EvidenceStorageSpace {storage.bytesAvailable(), storage.bytesTotal()};
      }),
      pngWriter_(pngWriter ? std::move(pngWriter)
                           : [](const QImage &image, const QString &path) {
          return AtomicPngWriter::write(image, path);
      })
{
    qRegisterMetaType<EvidenceRecord>();
    qRegisterMetaType<EvidenceCaptureAttempt>();
    qRegisterMetaType<EvidenceOperationResult>();
    qRegisterMetaType<EvidenceExportResult>();
    qRegisterMetaType<EvidenceFaultKind>();
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(-1);
}

EvidenceService::~EvidenceService()
{
    stopAccepting();
    pool_.waitForDone();
}

bool EvidenceService::initialize(QString *error)
{
    if (initialized_) {
        if (error) *error = storageError_;
        return writeEnabled_;
    }
    initialized_ = true;
    if (!QDir().mkpath(objectsRoot()) || !QDir().mkpath(orphanRoot())) {
        storageError_ = QStringLiteral("无法创建证据存储目录。");
        if (error) *error = storageError_;
        emit storageStateChanged(false, storageError_);
        return false;
    }
    const EvidenceCatalogLoadResult loaded = store_.load();
    if (!loaded.ok) {
        storageError_ = loaded.error;
        if (error) *error = storageError_;
        emit storageStateChanged(false, storageError_);
        return false;
    }
    records_ = loaded.records;
    attempts_ = loaded.captureAttempts;
    exportAudits_ = loaded.exportAudits;
    writeEnabled_ = true;
    recoverCatalog();
    quarantineOrphans();
    if (error) *error = storageError_;
    emit catalogChanged(records_, attempts_);
    return writeEnabled_;
}

bool EvidenceService::isInitialized() const noexcept { return initialized_; }
bool EvidenceService::isWriteEnabled() const noexcept { return writeEnabled_; }
QString EvidenceService::storageError() const { return storageError_; }
QString EvidenceService::rootPath() const { return rootPath_; }
QList<EvidenceRecord> EvidenceService::records() const { return records_; }
QList<EvidenceCaptureAttempt> EvidenceService::captureAttempts() const { return attempts_; }

QList<EvidenceRecord> EvidenceService::recordsForEvent(const QString &eventId) const
{
    QList<EvidenceRecord> result;
    for (const auto &record : records_)
        if (record.eventId == eventId) result.append(record);
    return result;
}

QList<EvidenceCaptureAttempt> EvidenceService::attemptsForEvent(const QString &eventId) const
{
    QList<EvidenceCaptureAttempt> result;
    for (const auto &attempt : attempts_)
        if (attempt.eventId == eventId) result.append(attempt);
    return result;
}

QHash<QString, QStringList> EvidenceService::evidenceProjection() const
{
    QHash<QString, QStringList> result;
    for (const auto &record : records_) result[record.eventId].append(record.evidenceId);
    for (auto it = result.begin(); it != result.end(); ++it) it.value().sort();
    return result;
}

QDateTime EvidenceService::nowUtc() const { return clock_().toUTC(); }
QString EvidenceService::objectsRoot() const { return QDir(rootPath_).filePath(QStringLiteral("objects")); }
QString EvidenceService::orphanRoot() const { return QDir(rootPath_).filePath(QStringLiteral("orphan")); }

bool EvidenceService::resolveRelativePath(const QString &relativePath,
                                          QString *absolutePath) const
{
    if (relativePath.trimmed().isEmpty() || QDir::isAbsolutePath(relativePath))
        return false;
    const QString clean = QDir::cleanPath(relativePath);
    if (clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../")))
        return false;
    const QString root = QDir::fromNativeSeparators(
        QDir(rootPath_).absolutePath());
    const QString candidate = QDir::fromNativeSeparators(
        QDir(rootPath_).absoluteFilePath(clean));
#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    const QString prefix = root.endsWith(QLatin1Char('/'))
        ? root : root + QLatin1Char('/');
    if (!candidate.startsWith(prefix, sensitivity)) return false;
    const QString canonicalRoot = QDir::fromNativeSeparators(
        QFileInfo(root).canonicalFilePath());
    QFileInfo ancestor(QFileInfo(candidate).absolutePath());
    while (!ancestor.exists() && ancestor.absoluteFilePath() != root)
        ancestor.setFile(ancestor.absolutePath());
    const QString canonicalAncestor = QDir::fromNativeSeparators(
        ancestor.canonicalFilePath());
    const QString canonicalPrefix = canonicalRoot.endsWith(QLatin1Char('/'))
        ? canonicalRoot : canonicalRoot + QLatin1Char('/');
    if (canonicalRoot.isEmpty() || canonicalAncestor.isEmpty() ||
        (canonicalAncestor.compare(canonicalRoot, sensitivity) != 0 &&
         !canonicalAncestor.startsWith(canonicalPrefix, sensitivity))) {
        return false;
    }
    if (absolutePath) *absolutePath = candidate;
    return true;
}

EvidenceCaptureFailure EvidenceService::preflightFailure(
    const EvidenceCaptureRequest &request, QString *message)
{
    EvidenceCaptureFailure failure = EvidenceCaptureFailure::None;
    if (request.eventId.trimmed().isEmpty() ||
        request.actor.trimmed().isEmpty() || request.streamId == 0)
        failure = EvidenceCaptureFailure::InvalidInput;
    else if (!accepting_) failure = EvidenceCaptureFailure::Stopped;
    else if (!initialized_ || !writeEnabled_) failure = EvidenceCaptureFailure::StorageUnavailable;
    else if (!request.playbackPlaying) failure = EvidenceCaptureFailure::PlaybackNotPlaying;
    else if (request.frameFreshnessMs < 0) failure = EvidenceCaptureFailure::FrameUnavailable;
    else if (request.frameFreshnessMs > 1000) failure = EvidenceCaptureFailure::FrameStale;
    else if (request.image.isNull()) failure = EvidenceCaptureFailure::EmptyImage;
    else if (pendingTasks_ >= kMaximumPendingTasks) failure = EvidenceCaptureFailure::QueueFull;
    if (failure == EvidenceCaptureFailure::None) {
        const EvidenceStorageSpace space = storageProbe_(rootPath_);
        const qint64 percentageFloor = space.bytesTotal > 0 ? space.bytesTotal / 20 : 0;
        const qint64 threshold = std::max(kMinimumFreeBytes, percentageFloor);
        if (space.bytesAvailable < 0 || space.bytesTotal <= 0)
            failure = EvidenceCaptureFailure::StorageUnavailable;
        else if (space.bytesAvailable < threshold)
            failure = EvidenceCaptureFailure::StorageLow;
        else
            emit subsystemRecoveryObserved(EvidenceFaultKind::StorageCapacity);
    }
    if (message) *message = operationMessage(failure);
    return failure;
}

EvidenceOperationResult EvidenceService::capture(EvidenceCaptureRequest request)
{
    if (!request.requestedAtUtc.isValid()) request.requestedAtUtc = nowUtc();
    if (!request.capturedAtUtc.isValid()) request.capturedAtUtc = request.requestedAtUtc;
    QString message;
    const EvidenceCaptureFailure failure = preflightFailure(request, &message);
    if (failure != EvidenceCaptureFailure::None)
        return recordRejectedCapture(request, failure, message);

    const QString attemptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString evidenceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString relativePath = QStringLiteral("objects/%1/%2.png")
        .arg(evidenceId.left(2), evidenceId);
    QString absolutePath;
    if (!resolveRelativePath(relativePath, &absolutePath))
        return recordRejectedCapture(request, EvidenceCaptureFailure::DirectoryCreateFailed,
                                     operationMessage(EvidenceCaptureFailure::DirectoryCreateFailed));
    const QImage thumbnail = request.image.scaled(
        QSize(320, 180), Qt::KeepAspectRatio, Qt::FastTransformation);
    ++pendingTasks_;
    const quint64 generation = generation_;
    const PngWriter writer = pngWriter_;
    QPointer<EvidenceService> service(this);
    pool_.start(QRunnable::create(
        [service, request = std::move(request), attemptId, evidenceId,
         relativePath, absolutePath, thumbnail, generation, writer]() mutable {
            const AtomicPngWriteResult write = writer(request.image, absolutePath);
            if (QCoreApplication *application = QCoreApplication::instance()) {
                QMetaObject::invokeMethod(application,
                    [service, request = std::move(request), attemptId, evidenceId,
                     relativePath, thumbnail, write, generation]() mutable {
                        if (service == nullptr || service->generation_ != generation) return;
                        service->finishCapture(request, attemptId, evidenceId,
                                               relativePath, thumbnail,
                                               write.succeeded, write.sizeBytes,
                                               write.error);
                    }, Qt::QueuedConnection);
            }
        }));
    return {true, attemptId, EvidenceCaptureFailure::None, {}};
}

EvidenceOperationResult EvidenceService::recordRejectedCapture(
    const EvidenceCaptureRequest &request, EvidenceCaptureFailure failure,
    const QString &message)
{
    EvidenceCaptureAttempt attempt;
    attempt.attemptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    attempt.eventId = request.eventId;
    attempt.deviceId = request.deviceId;
    attempt.streamId = request.streamId;
    attempt.requestedAtUtc = request.requestedAtUtc.isValid()
        ? request.requestedAtUtc.toUTC() : nowUtc();
    attempt.completedAtUtc = nowUtc();
    attempt.frameFreshnessMs = request.frameFreshnessMs;
    attempt.sourcePlaybackState = request.sourcePlaybackState.isEmpty()
        ? QStringLiteral("unknown") : request.sourcePlaybackState;
    attempt.failure = failure;
    attempt.actor = request.actor.trimmed().isEmpty()
        ? QStringLiteral("local-user") : request.actor.trimmed();
    attempt.actorAssurance = request.actorAssurance;
    if (initialized_ && writeEnabled_) {
        QList<EvidenceCaptureAttempt> candidate = attempts_;
        candidate.append(attempt);
        QString error;
        const bool stored = commit(records_, candidate, exportAudits_, &error);
        if (!stored) emit subsystemFaultObserved(EvidenceFaultKind::Catalog, error);
    }
    if (failure == EvidenceCaptureFailure::StorageLow)
        emit subsystemFaultObserved(EvidenceFaultKind::StorageCapacity, message);
    emit captureFailed(attempt, message);
    return {false, attempt.attemptId, failure, message};
}

void EvidenceService::finishCapture(
    const EvidenceCaptureRequest &request, const QString &attemptId,
    const QString &evidenceId, const QString &relativePath,
    const QImage &thumbnail, bool writeSucceeded, qint64 sizeBytes,
    const QString &writeError)
{
    pendingTasks_ = std::max(0, pendingTasks_ - 1);
    EvidenceCaptureAttempt attempt;
    attempt.attemptId = attemptId;
    attempt.eventId = request.eventId;
    attempt.evidenceId = writeSucceeded ? evidenceId : QString();
    attempt.deviceId = request.deviceId;
    attempt.streamId = request.streamId;
    attempt.requestedAtUtc = request.requestedAtUtc.toUTC();
    attempt.completedAtUtc = nowUtc();
    attempt.frameFreshnessMs = request.frameFreshnessMs;
    attempt.sourcePlaybackState = request.sourcePlaybackState;
    attempt.succeeded = writeSucceeded;
    attempt.failure = writeSucceeded ? EvidenceCaptureFailure::None
                                     : writeFailure(writeError);
    attempt.actor = request.actor;
    attempt.actorAssurance = request.actorAssurance;

    QList<EvidenceRecord> candidateRecords = records_;
    if (writeSucceeded) {
        EvidenceRecord record;
        record.evidenceId = evidenceId;
        record.eventId = request.eventId;
        record.deviceId = request.deviceId;
        record.streamId = request.streamId;
        record.identitySource = request.identitySource;
        record.captureRequestedAtUtc = request.requestedAtUtc.toUTC();
        record.capturedAtUtc = request.capturedAtUtc.toUTC();
        record.writtenAtUtc = attempt.completedAtUtc;
        record.frameFreshnessMs = request.frameFreshnessMs;
        record.sourcePlaybackState = request.sourcePlaybackState;
        record.relativePath = relativePath;
        record.sizeBytes = sizeBytes;
        record.actor = request.actor;
        record.actorAssurance = request.actorAssurance;
        record.lastVerifiedAtUtc = attempt.completedAtUtc;
        candidateRecords.append(record);
    }
    QList<EvidenceCaptureAttempt> candidateAttempts = attempts_;
    candidateAttempts.append(attempt);
    QString commitError;
    if (!commit(candidateRecords, candidateAttempts, exportAudits_, &commitError)) {
        attempt.succeeded = false;
        attempt.failure = EvidenceCaptureFailure::CatalogCommitFailed;
        emit subsystemFaultObserved(EvidenceFaultKind::Catalog, commitError);
        emit captureFailed(attempt, operationMessage(attempt.failure));
        return;
    }
    if (!writeSucceeded) {
        emit subsystemFaultObserved(EvidenceFaultKind::ObjectWrite, writeError);
        emit captureFailed(attempt, writeError.isEmpty() ? operationMessage(attempt.failure)
                                                         : writeError);
        return;
    }
    emit subsystemRecoveryObserved(EvidenceFaultKind::ObjectWrite);
    emit subsystemRecoveryObserved(EvidenceFaultKind::Catalog);
    emit captureCompleted(records_.back(), thumbnail);
}

bool EvidenceService::commit(
    const QList<EvidenceRecord> &records,
    const QList<EvidenceCaptureAttempt> &attempts,
    const QList<EvidenceExportAudit> &audits,
    QString *error)
{
    QString saveError;
    if (!store_.save(records, attempts, audits, nowUtc(), &saveError)) {
        writeEnabled_ = false;
        storageError_ = saveError;
        if (error) *error = saveError;
        emit storageStateChanged(false, storageError_);
        return false;
    }
    records_ = records;
    attempts_ = attempts;
    exportAudits_ = audits;
    emit catalogChanged(records_, attempts_);
    return true;
}

EvidenceOperationResult EvidenceService::exportIncident(IncidentExportRequest request)
{
    if (!accepting_)
        return {false, {}, EvidenceCaptureFailure::Stopped,
                operationMessage(EvidenceCaptureFailure::Stopped)};
    if (!initialized_ || !writeEnabled_)
        return {false, {}, EvidenceCaptureFailure::StorageUnavailable,
                operationMessage(EvidenceCaptureFailure::StorageUnavailable)};
    if (request.eventId.trimmed().isEmpty() ||
        request.destinationParentDirectory.trimmed().isEmpty() ||
        request.actor.trimmed().isEmpty()) {
        return {false, {}, EvidenceCaptureFailure::InvalidInput,
                QStringLiteral("导出参数不完整。")};
    }
    if (pendingTasks_ >= kMaximumPendingTasks)
        return {false, {}, EvidenceCaptureFailure::QueueFull,
                operationMessage(EvidenceCaptureFailure::QueueFull)};

    const QList<EvidenceRecord> records = recordsForEvent(request.eventId);
    const QList<EvidenceCaptureAttempt> attempts = attemptsForEvent(request.eventId);
    for (const auto &record : records) {
        QString path;
        if (record.availability != EvidenceAvailability::Available ||
            !resolveRelativePath(record.relativePath, &path) || !QFileInfo::exists(path)) {
            emit subsystemFaultObserved(EvidenceFaultKind::Consistency,
                                        QStringLiteral("事件包含不可用证据，拒绝导出。"));
            return {false, {}, EvidenceCaptureFailure::StorageUnavailable,
                    QStringLiteral("事件包含不可用证据，无法导出完整目录。")};
        }
    }

    const QString exportId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ++pendingTasks_;
    const quint64 generation = generation_;
    const QString root = rootPath_;
    QPointer<EvidenceService> service(this);
    pool_.start(QRunnable::create(
        [service, request = std::move(request), records, attempts, exportId,
         root, generation]() mutable {
            QString error;
            QString finalPath;
            const QString parentPath = QDir::cleanPath(request.destinationParentDirectory);
            QDir parent(parentPath);
            bool succeeded = parent.exists();
            if (!succeeded) error = QStringLiteral("导出目标父目录不存在。");
            const QString stamp = QDateTime::currentDateTimeUtc().toString(
                QStringLiteral("yyyyMMdd-HHmmss-zzz"));
            QString finalName = QStringLiteral("incident-%1-%2")
                .arg(safeLeaf(request.eventId), stamp);
            int suffix = 1;
            while (parent.exists(finalName))
                finalName = QStringLiteral("incident-%1-%2-%3")
                    .arg(safeLeaf(request.eventId), stamp).arg(++suffix);
            const QString tempName = QStringLiteral(".rtmpmonitor-export-%1.tmp")
                .arg(exportId);
            const QString tempPath = parent.filePath(tempName);
            if (succeeded && !parent.mkdir(tempName)) {
                succeeded = false;
                error = QStringLiteral("无法创建导出临时目录。");
            }
            QDir temp(tempPath);
            if (succeeded && (!temp.mkdir(QStringLiteral("evidence")) ||
                              !temp.mkdir(QStringLiteral("audit")))) {
                succeeded = false;
                error = QStringLiteral("无法创建导出目录结构。");
            }
            QJsonArray evidenceJson;
            if (succeeded) {
                for (const auto &record : records) {
                    const QString source = QDir(root).filePath(record.relativePath);
                    const QString target = temp.filePath(
                        QStringLiteral("evidence/%1.png").arg(record.evidenceId));
                    if (!copyFileAtomically(source, target, &error)) {
                        succeeded = false;
                        break;
                    }
                    EvidenceRecord exported = record;
                    exported.relativePath = QStringLiteral("evidence/%1.png")
                                                .arg(record.evidenceId);
                    evidenceJson.append(evidenceRecordToJson(exported));
                }
            }
            if (succeeded) {
                QSaveFile controls(temp.filePath(QStringLiteral("audit/control-attempts.jsonl")));
                if (!controls.open(QIODevice::WriteOnly)) {
                    succeeded = false;
                    error = QStringLiteral("无法创建控制尝试导出文件。");
                } else {
                    for (const auto &entry : request.linkedControlAttempts) {
                        const QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
                        if (controls.write(line) != line.size()) {
                            succeeded = false;
                            error = QStringLiteral("无法写入控制尝试导出文件。");
                            controls.cancelWriting();
                            break;
                        }
                    }
                    if (succeeded && !controls.commit()) {
                        succeeded = false;
                        error = QStringLiteral("无法原子提交控制尝试导出文件。");
                    }
                }
            }
            if (succeeded) {
                QJsonArray attemptJson;
                for (const auto &attempt : attempts)
                    attemptJson.append(evidenceCaptureAttemptToJson(attempt));
                const QJsonObject manifest {
                    {QStringLiteral("schemaVersion"), 1},
                    {QStringLiteral("exportedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                    {QStringLiteral("eventRevision"), static_cast<double>(request.eventRevision)},
                    {QStringLiteral("actor"), request.actor},
                    {QStringLiteral("actorAssurance"), request.actorAssurance},
                    {QStringLiteral("contentHashVerification"), QStringLiteral("not_performed")},
                    {QStringLiteral("notice"), QStringLiteral("本目录为本机事件资料副本，未进行内容哈希、数字签名或可信时间戳校验。")},
                    {QStringLiteral("event"), request.eventSnapshot},
                    {QStringLiteral("stateHistory"), request.stateHistory},
                    {QStringLiteral("evidence"), evidenceJson},
                    {QStringLiteral("captureAttempts"), attemptJson},
                };
                QSaveFile manifestFile(temp.filePath(QStringLiteral("manifest.json")));
                const QByteArray data = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
                if (!manifestFile.open(QIODevice::WriteOnly) ||
                    manifestFile.write(data) != data.size() ||
                    !manifestFile.commit()) {
                    succeeded = false;
                    error = QStringLiteral("无法原子提交导出 manifest。");
                }
            }
            if (succeeded) {
                if (!parent.rename(tempName, finalName)) {
                    succeeded = false;
                    error = QStringLiteral("无法发布最终导出目录。");
                } else {
                    finalPath = parent.filePath(finalName);
                }
            }
            if (!succeeded && QDir(tempPath).exists())
                QDir(tempPath).removeRecursively();
            if (QCoreApplication *application = QCoreApplication::instance()) {
                QMetaObject::invokeMethod(application,
                    [service, request = std::move(request), exportId, succeeded,
                     finalPath, error, generation]() mutable {
                        if (service == nullptr || service->generation_ != generation) return;
                        service->finishExport(request, exportId, succeeded,
                                              finalPath, error);
                    }, Qt::QueuedConnection);
            }
        }));
    return {true, exportId, EvidenceCaptureFailure::None, {}};
}

void EvidenceService::finishExport(const IncidentExportRequest &request,
                                   const QString &exportId, bool succeeded,
                                   const QString &outputDirectory,
                                   const QString &message)
{
    pendingTasks_ = std::max(0, pendingTasks_ - 1);
    EvidenceExportAudit audit;
    audit.exportId = exportId;
    audit.eventId = request.eventId;
    audit.eventRevision = request.eventRevision;
    audit.exportedAtUtc = nowUtc();
    audit.actor = request.actor;
    audit.actorAssurance = request.actorAssurance;
    audit.succeeded = succeeded;
    audit.outputDirectoryName = QFileInfo(outputDirectory).fileName();
    audit.failureReason = message;
    QList<EvidenceExportAudit> audits = exportAudits_;
    audits.append(audit);
    QString commitError;
    if (!commit(records_, attempts_, audits, &commitError)) {
        emit subsystemFaultObserved(EvidenceFaultKind::Catalog, commitError);
        emit exportCompleted({false, exportId, request.eventId, outputDirectory,
                              QStringLiteral("导出目录已处理，但审计记录提交失败：%1").arg(commitError)});
        return;
    }
    emit exportCompleted({succeeded, exportId, request.eventId,
                          outputDirectory, message});
}

void EvidenceService::recoverCatalog()
{
    bool changed = false;
    bool inconsistent = false;
    const QDateTime verifiedAt = nowUtc();
    QList<EvidenceRecord> candidate = records_;
    for (auto &record : candidate) {
        QString path;
        EvidenceAvailability availability = EvidenceAvailability::Available;
        if (!resolveRelativePath(record.relativePath, &path))
            availability = EvidenceAvailability::UnsafePath;
        else if (!QFileInfo::exists(path))
            availability = EvidenceAvailability::Missing;
        if (record.availability != availability ||
            record.lastVerifiedAtUtc != verifiedAt) {
            record.availability = availability;
            record.lastVerifiedAtUtc = verifiedAt;
            changed = true;
        }
        inconsistent = inconsistent || availability != EvidenceAvailability::Available;
    }
    if (changed) {
        QString error;
        if (!commit(candidate, attempts_, exportAudits_, &error)) {
            emit subsystemFaultObserved(EvidenceFaultKind::Catalog, error);
            return;
        }
    }
    if (inconsistent)
        emit subsystemFaultObserved(EvidenceFaultKind::Consistency,
                                    QStringLiteral("证据目录存在缺失文件或不安全路径。"));
    else
        emit subsystemRecoveryObserved(EvidenceFaultKind::Consistency);
}

void EvidenceService::quarantineOrphans()
{
    QSet<QString> registered;
    for (const auto &record : records_) {
        QString path;
        if (resolveRelativePath(record.relativePath, &path))
            registered.insert(QDir::cleanPath(path));
    }
    bool found = false;
    QDirIterator iterator(objectsRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = QDir::cleanPath(iterator.next());
        if (registered.contains(path)) continue;
        found = true;
        const QString destination = QDir(orphanRoot()).filePath(
            QStringLiteral("%1-%2")
                .arg(nowUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")),
                     QFileInfo(path).fileName()));
        QFile::rename(path, destination);
    }
    if (found)
        emit subsystemFaultObserved(EvidenceFaultKind::Consistency,
                                    QStringLiteral("发现未登记证据文件，已移入 orphan 隔离区。"));
}

void EvidenceService::stopAccepting()
{
    if (!accepting_) return;
    accepting_ = false;
    ++generation_;
    pool_.clear();
    pool_.waitForDone(2000);
}
