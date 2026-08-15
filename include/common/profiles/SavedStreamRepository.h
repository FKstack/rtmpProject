#pragma once

#include <QList>
#include <QString>

#include "profiles/SavedStreamProfile.h"

struct SavedStreamLoadResult
{
    QList<SavedStreamProfile> profiles;
    QString error;
    bool fileExists = false;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

/** Owns validation and atomic persistence of user-managed RTMP profiles. */
class SavedStreamRepository final
{
public:
    static constexpr int kSchemaVersion = 1;
    static constexpr int kMaximumProfiles = 16;

    explicit SavedStreamRepository(QString filePath = {});

    [[nodiscard]] SavedStreamLoadResult load() const;
    [[nodiscard]] bool save(const QList<SavedStreamProfile> &profiles,
                            QString *error = nullptr) const;
    [[nodiscard]] static bool validate(
        const QList<SavedStreamProfile> &profiles,
        QString *error = nullptr
    );
    [[nodiscard]] QString filePath() const;

private:
    QString filePath_;
};
