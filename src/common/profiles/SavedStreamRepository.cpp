#include "profiles/SavedStreamRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include <utility>

namespace {

QString defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           QStringLiteral("/saved-streams.json");
}

QString normalizedUrl(const QString &value)
{
    QUrl url(value.trimmed(), QUrl::StrictMode);
    url.setScheme(url.scheme().toLower());
    url.setHost(url.host().toLower());
    return url.toString(QUrl::FullyEncoded);
}

void assignError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

} // namespace

SavedStreamRepository::SavedStreamRepository(QString filePath)
    : filePath_(filePath.trimmed().isEmpty() ? defaultPath()
                                             : std::move(filePath))
{
}

SavedStreamLoadResult SavedStreamRepository::load() const
{
    SavedStreamLoadResult result;
    QFile file(filePath_);
    result.fileExists = file.exists();
    if (!result.fileExists) {
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取保存的推流列表：%1")
                           .arg(file.errorString());
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        result.error = QStringLiteral("保存的推流列表 JSON 无效：%1")
                           .arg(parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != kSchemaVersion) {
        result.error = schemaVersion > kSchemaVersion
            ? QStringLiteral("保存的推流列表版本 %1 高于当前支持的版本 %2。")
                  .arg(schemaVersion).arg(kSchemaVersion)
            : QStringLiteral("保存的推流列表缺少受支持的 schemaVersion。");
        return result;
    }
    const QJsonValue profilesValue = root.value(QStringLiteral("profiles"));
    if (!profilesValue.isArray()) {
        result.error = QStringLiteral("保存的推流列表缺少 profiles 数组。");
        return result;
    }

    for (const QJsonValue &value : profilesValue.toArray()) {
        if (!value.isObject()) {
            result.error = QStringLiteral("保存的推流列表包含非对象条目。");
            result.profiles.clear();
            return result;
        }
        const QJsonObject object = value.toObject();
        result.profiles.push_back({
            object.value(QStringLiteral("profileId")).toString(),
            object.value(QStringLiteral("displayName")).toString(),
            object.value(QStringLiteral("streamUrl")).toString(),
            object.value(QStringLiteral("autoConnect")).toBool(true)
        });
    }
    if (!validate(result.profiles, &result.error)) {
        result.profiles.clear();
    }
    return result;
}

bool SavedStreamRepository::save(
    const QList<SavedStreamProfile> &profiles,
    QString *error
) const
{
    if (!validate(profiles, error)) {
        return false;
    }
    const QFileInfo info(filePath_);
    QDir directory(info.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        assignError(error, QStringLiteral("无法创建保存目录。"));
        return false;
    }

    QJsonArray array;
    for (const SavedStreamProfile &profile : profiles) {
        array.append(QJsonObject{
            {QStringLiteral("profileId"), profile.profileId},
            {QStringLiteral("displayName"), profile.displayName.trimmed()},
            {QStringLiteral("streamUrl"), profile.streamUrl.trimmed()},
            {QStringLiteral("autoConnect"), profile.autoConnect}
        });
    }
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("profiles"), array}
    });

    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(document.toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) {
        assignError(error, QStringLiteral("无法原子保存推流列表：%1")
                               .arg(file.errorString()));
        return false;
    }
    assignError(error, {});
    return true;
}

bool SavedStreamRepository::validate(
    const QList<SavedStreamProfile> &profiles,
    QString *error
)
{
    if (profiles.size() > kMaximumProfiles) {
        assignError(error, QStringLiteral("保存的推流最多允许 %1 条。")
                               .arg(kMaximumProfiles));
        return false;
    }
    QSet<QString> ids;
    QSet<QString> urls;
    for (const SavedStreamProfile &profile : profiles) {
        if (QUuid(profile.profileId).isNull()) {
            assignError(error, QStringLiteral("推流档案包含无效 profileId。"));
            return false;
        }
        const QString name = profile.displayName.trimmed();
        if (name.isEmpty() || name.size() > 64) {
            assignError(error, QStringLiteral("推流名称必须为 1～64 个字符。"));
            return false;
        }
        const QUrl url(profile.streamUrl.trimmed(), QUrl::StrictMode);
        if (!url.isValid() || url.scheme().compare(
                QStringLiteral("rtmp"), Qt::CaseInsensitive) != 0 ||
            url.host().isEmpty()) {
            assignError(error, QStringLiteral("保存的地址必须是有效的 rtmp:// URL。"));
            return false;
        }
        const QString id = profile.profileId.toLower();
        const QString canonicalUrl = normalizedUrl(profile.streamUrl);
        if (ids.contains(id)) {
            assignError(error, QStringLiteral("推流档案包含重复 profileId。"));
            return false;
        }
        if (urls.contains(canonicalUrl)) {
            assignError(error, QStringLiteral("同一 RTMP 地址不能重复保存。"));
            return false;
        }
        ids.insert(id);
        urls.insert(canonicalUrl);
    }
    assignError(error, {});
    return true;
}

QString SavedStreamRepository::filePath() const
{
    return filePath_;
}
