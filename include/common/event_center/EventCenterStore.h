#pragma once

#include <QList>
#include <QString>

#include "event_center/EventCenterTypes.h"

struct EventStoreLoadResult
{
    bool ok = false;
    bool writeBlocked = false;
    QList<SecurityEventRecord> events;
    QList<SecurityEventTombstone> tombstones;
    QString error;
};

/** Owns schema-v1/v2 JSON parsing and QSaveFile atomic replacement. */
class EventCenterStore final
{
public:
    static constexpr int kSchemaVersion = 2;

    explicit EventCenterStore(QString filePath = {});

    [[nodiscard]] EventStoreLoadResult load() const;
    [[nodiscard]] bool save(
        const QList<SecurityEventRecord> &events,
        const QList<SecurityEventTombstone> &tombstones,
        const QDateTime &savedAtUtc,
        QString *error = nullptr
    ) const;

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] static QString defaultFilePath();

private:
    QString filePath_;
};
