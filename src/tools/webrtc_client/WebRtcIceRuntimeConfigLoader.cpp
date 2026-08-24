#include "webrtc_client/WebRtcIceRuntimeConfigLoader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <rtc/rtc.hpp>

#include <utility>

namespace rtmp_monitor::webrtc_client {
namespace {

constexpr qint64 kMaximumConfigurationBytes = 4 * 1024;

bool containsPlaceholderOrWhitespace(const QString &value)
{
    if (value.contains(QLatin1Char('<')) ||
        value.contains(QLatin1Char('>'))) {
        return true;
    }
    for (const QChar character : value) {
        if (character.isSpace() || character.isNull()) return true;
    }
    return false;
}

} // namespace

IceConfigLoadResult WebRtcIceRuntimeConfigLoader::load(const QString &path)
{
    QFile file(path);
    if (!file.exists()) return {IceConfigLoadError::NotFound, {}};
    if (!file.open(QIODevice::ReadOnly)) {
        return {IceConfigLoadError::ReadFailed, {}};
    }
    if (file.size() <= 0 || file.size() > kMaximumConfigurationBytes) {
        return {IceConfigLoadError::InvalidConfiguration, {}};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        file.readAll(), &parseError
    );
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {IceConfigLoadError::InvalidConfiguration, {}};
    }
    const QJsonObject object = document.object();
    const QSet<QString> expected {
        QStringLiteral("schemaVersion"), QStringLiteral("stunUrl")
    };
    QSet<QString> actual;
    for (const QString &key : object.keys()) actual.insert(key);
    if (actual != expected) {
        return {IceConfigLoadError::InvalidConfiguration, {}};
    }
    if (!object.value(QStringLiteral("schemaVersion")).isDouble()) {
        return {IceConfigLoadError::InvalidConfiguration, {}};
    }
    if (object.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        return {IceConfigLoadError::UnsupportedVersion, {}};
    }
    if (!object.value(QStringLiteral("stunUrl")).isString()) {
        return {IceConfigLoadError::InvalidConfiguration, {}};
    }
    const QString url = object.value(QStringLiteral("stunUrl")).toString();
    if (url.isEmpty() || url.size() > 512 ||
        !url.startsWith(QStringLiteral("stun:"), Qt::CaseInsensitive) ||
        containsPlaceholderOrWhitespace(url)) {
        return {IceConfigLoadError::InvalidConfiguration, {}};
    }
    try {
        const rtc::IceServer parsed(url.toStdString());
        if (parsed.type != rtc::IceServer::Type::Stun) {
            return {IceConfigLoadError::InvalidConfiguration, {}};
        }
    } catch (...) {
        return {IceConfigLoadError::InvalidConfiguration, {}};
    }

    IceRuntimeConfig configuration;
    IceServerRuntimeConfig server;
    server.urls.push_back(url.toStdString());
    configuration.servers.push_back(std::move(server));
    return {IceConfigLoadError::None, std::move(configuration)};
}

const char *WebRtcIceRuntimeConfigLoader::errorName(
    IceConfigLoadError error
) noexcept
{
    switch (error) {
    case IceConfigLoadError::None: return "none";
    case IceConfigLoadError::NotFound: return "ice_config_not_found";
    case IceConfigLoadError::ReadFailed: return "ice_config_read_failed";
    case IceConfigLoadError::InvalidConfiguration: return "invalid_ice_config";
    case IceConfigLoadError::UnsupportedVersion:
        return "unsupported_ice_config_version";
    }
    return "invalid_ice_config";
}

} // namespace rtmp_monitor::webrtc_client
