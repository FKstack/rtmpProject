#include "logging/SensitiveDataSanitizer.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

namespace {

bool isSensitiveKey(const QString &key)
{
    const QString normalized = key.toLower();
    return normalized.contains(QStringLiteral("password")) ||
           normalized.contains(QStringLiteral("passwd")) ||
           normalized.contains(QStringLiteral("token")) ||
           normalized.contains(QStringLiteral("secret")) ||
           normalized.contains(QStringLiteral("privatekey")) ||
           normalized.contains(QStringLiteral("private_key")) ||
           normalized.contains(QStringLiteral("credential"));
}

QJsonValue sanitizeValue(const QString &key, const QJsonValue &value)
{
    if (isSensitiveKey(key)) {
        return QStringLiteral("***");
    }
    if (value.isObject()) {
        return SensitiveDataSanitizer::sanitizeObject(value.toObject());
    }
    if (value.isArray()) {
        QJsonArray result;
        for (const QJsonValue &item : value.toArray()) {
            if (item.isObject()) {
                result.append(
                    SensitiveDataSanitizer::sanitizeObject(item.toObject())
                );
            } else if (item.isString()) {
                result.append(
                    SensitiveDataSanitizer::sanitizeText(item.toString())
                );
            } else {
                result.append(item);
            }
        }
        return result;
    }
    if (value.isString()) {
        return SensitiveDataSanitizer::sanitizeText(value.toString());
    }
    return value;
}

} // namespace

QString SensitiveDataSanitizer::sanitizeUrl(const QString &url)
{
    if (url.trimmed().isEmpty()) {
        return {};
    }
    QUrl sanitized(url.trimmed(), QUrl::StrictMode);
    if (!sanitized.isValid() || sanitized.scheme().isEmpty()) {
        return QStringLiteral("<invalid-url>");
    }
    sanitized.setUserInfo(QString());
    sanitized.setQuery(QString());
    sanitized.setFragment(QString());

    QStringList segments = sanitized.path().split(
        QLatin1Char('/'), Qt::SkipEmptyParts
    );
    if (!segments.isEmpty()) {
        segments.last() = QStringLiteral("***");
        sanitized.setPath(
            QStringLiteral("/") + segments.join(QLatin1Char('/'))
        );
    }
    return sanitized.toString(QUrl::FullyEncoded);
}

QString SensitiveDataSanitizer::sanitizeText(const QString &text)
{
    QString result = text;
    static const QRegularExpression urlPattern(
        QStringLiteral(R"(([A-Za-z][A-Za-z0-9+.-]*://[^\s"'<>]+))")
    );
    int offset = 0;
    while (true) {
        const QRegularExpressionMatch match =
            urlPattern.match(result, offset);
        if (!match.hasMatch()) {
            break;
        }
        const QString replacement = sanitizeUrl(match.captured(1));
        result.replace(
            match.capturedStart(1),
            match.capturedLength(1),
            replacement
        );
        offset = match.capturedStart(1) + replacement.size();
    }

    static const QRegularExpression secretPattern(
        QStringLiteral(
            R"((password|passwd|token|secret|private[_-]?key|credential)\s*([=:])\s*([^\s,;]+))"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    result.replace(secretPattern, QStringLiteral("\\1\\2***"));
    return result;
}

QJsonObject SensitiveDataSanitizer::sanitizeObject(
    const QJsonObject &object
)
{
    QJsonObject result;
    for (auto iterator = object.begin(); iterator != object.end();
         ++iterator) {
        result.insert(
            iterator.key(),
            sanitizeValue(iterator.key(), iterator.value())
        );
    }
    return result;
}
