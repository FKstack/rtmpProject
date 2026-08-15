#include "device_control/DeviceIdentity.h"

#include <QRegularExpression>
#include <QUrl>

bool DeviceIdentity::isValid(const QString &deviceId)
{
    const QString normalized = deviceId.trimmed();
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
    return pattern.match(normalized).hasMatch();
}

std::optional<QString> DeviceIdentity::fromRtmpUrl(const QString &streamUrl)
{
    const QUrl url(streamUrl.trimmed(), QUrl::StrictMode);
    if (!url.isValid() ||
        url.scheme().compare(QStringLiteral("rtmp"), Qt::CaseInsensitive) != 0 ||
        url.host().isEmpty()) {
        return std::nullopt;
    }
    const QStringList segments = url.path(QUrl::FullyEncoded).split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.isEmpty()) return std::nullopt;
    const QString deviceId = QUrl::fromPercentEncoding(
        segments.constLast().toUtf8()).trimmed();
    return isValid(deviceId) ? std::optional<QString>(deviceId) : std::nullopt;
}
