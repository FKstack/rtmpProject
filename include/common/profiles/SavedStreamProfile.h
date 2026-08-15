#pragma once

#include <QString>

struct SavedStreamProfile
{
    QString profileId;
    QString displayName;
    QString streamUrl;
    bool autoConnect = true;
};

inline bool operator==(const SavedStreamProfile &left,
                       const SavedStreamProfile &right) noexcept
{
    return left.profileId == right.profileId &&
           left.displayName == right.displayName &&
           left.streamUrl == right.streamUrl &&
           left.autoConnect == right.autoConnect;
}
