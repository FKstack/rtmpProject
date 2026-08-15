#pragma once

#include <QImage>
#include <QString>

struct AtomicPngWriteResult
{
    bool succeeded = false;
    qint64 sizeBytes = 0;
    QString error;
};

/** Performs one synchronous QSaveFile PNG commit; callers own threading. */
class AtomicPngWriter final
{
public:
    [[nodiscard]] static AtomicPngWriteResult write(
        const QImage &image, const QString &absolutePath);
};
