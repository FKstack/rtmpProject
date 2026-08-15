#pragma once

#include <QList>
#include <QString>

#include "evidence/EvidenceTypes.h"

struct EvidenceCatalogLoadResult
{
    bool ok = false;
    bool writeBlocked = false;
    QList<EvidenceRecord> records;
    QList<EvidenceCaptureAttempt> captureAttempts;
    QList<EvidenceExportAudit> exportAudits;
    QString error;
};

/** Owns evidence catalog schema-v1 JSON and atomic replacement. */
class EvidenceCatalogStore final
{
public:
    static constexpr int kSchemaVersion = 1;

    explicit EvidenceCatalogStore(QString filePath = {});
    [[nodiscard]] EvidenceCatalogLoadResult load() const;
    [[nodiscard]] bool save(
        const QList<EvidenceRecord> &records,
        const QList<EvidenceCaptureAttempt> &attempts,
        const QList<EvidenceExportAudit> &audits,
        const QDateTime &savedAtUtc,
        QString *error = nullptr) const;
    [[nodiscard]] QString filePath() const;

private:
    QString filePath_;
};
