#pragma once

#include <QHash>
#include <QObject>
#include <QThreadPool>

#include <functional>

#include "evidence/EvidenceCatalogStore.h"
#include "evidence/AtomicPngWriter.h"

struct EvidenceStorageSpace
{
    qint64 bytesAvailable = -1;
    qint64 bytesTotal = -1;
};

/** Owns evidence files, catalog state, bounded I/O and export lifecycle. */
class EvidenceService final : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;
    using StorageProbe = std::function<EvidenceStorageSpace(const QString &)>;
    using PngWriter = std::function<AtomicPngWriteResult(
        const QImage &, const QString &)>;

    explicit EvidenceService(QString rootPath = {},
                             Clock clock = {},
                             StorageProbe storageProbe = {},
                             PngWriter pngWriter = {},
                             QObject *parent = nullptr);
    ~EvidenceService() override;

    [[nodiscard]] bool initialize(QString *error = nullptr);
    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] bool isWriteEnabled() const noexcept;
    [[nodiscard]] QString storageError() const;
    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] QList<EvidenceRecord> records() const;
    [[nodiscard]] QList<EvidenceCaptureAttempt> captureAttempts() const;
    [[nodiscard]] QList<EvidenceRecord> recordsForEvent(const QString &eventId) const;
    [[nodiscard]] QList<EvidenceCaptureAttempt> attemptsForEvent(const QString &eventId) const;
    [[nodiscard]] QHash<QString, QStringList> evidenceProjection() const;

    EvidenceOperationResult capture(EvidenceCaptureRequest request);
    EvidenceOperationResult exportIncident(IncidentExportRequest request);
    void stopAccepting();

signals:
    void catalogChanged(const QList<EvidenceRecord> &records,
                        const QList<EvidenceCaptureAttempt> &attempts);
    void storageStateChanged(bool writeEnabled, const QString &error);
    void captureCompleted(const EvidenceRecord &record, const QImage &thumbnail);
    void captureFailed(const EvidenceCaptureAttempt &attempt,
                       const QString &message);
    void exportCompleted(const EvidenceExportResult &result);
    void subsystemFaultObserved(EvidenceFaultKind kind, const QString &message);
    void subsystemRecoveryObserved(EvidenceFaultKind kind);

private:
    static constexpr int kMaximumPendingTasks = 4;
    static constexpr qint64 kMinimumFreeBytes = 2LL * 1024 * 1024 * 1024;

    [[nodiscard]] QDateTime nowUtc() const;
    [[nodiscard]] QString objectsRoot() const;
    [[nodiscard]] QString orphanRoot() const;
    [[nodiscard]] bool resolveRelativePath(const QString &relativePath,
                                           QString *absolutePath) const;
    [[nodiscard]] EvidenceCaptureFailure preflightFailure(
        const EvidenceCaptureRequest &request, QString *message);
    [[nodiscard]] bool commit(const QList<EvidenceRecord> &records,
                              const QList<EvidenceCaptureAttempt> &attempts,
                              const QList<EvidenceExportAudit> &audits,
                              QString *error);
    EvidenceOperationResult recordRejectedCapture(
        const EvidenceCaptureRequest &request,
        EvidenceCaptureFailure failure,
        const QString &message);
    void finishCapture(const EvidenceCaptureRequest &request,
                       const QString &attemptId,
                       const QString &evidenceId,
                       const QString &relativePath,
                       const QImage &thumbnail,
                       bool writeSucceeded,
                       qint64 sizeBytes,
                       const QString &writeError);
    void finishExport(const IncidentExportRequest &request,
                      const QString &exportId,
                      bool succeeded,
                      const QString &outputDirectory,
                      const QString &message);
    void recoverCatalog();
    void quarantineOrphans();

    QString rootPath_;
    EvidenceCatalogStore store_;
    Clock clock_;
    StorageProbe storageProbe_;
    PngWriter pngWriter_;
    QThreadPool pool_;
    QList<EvidenceRecord> records_;
    QList<EvidenceCaptureAttempt> attempts_;
    QList<EvidenceExportAudit> exportAudits_;
    QString storageError_;
    int pendingTasks_ = 0;
    quint64 generation_ = 1;
    bool initialized_ = false;
    bool writeEnabled_ = false;
    bool accepting_ = true;
};
