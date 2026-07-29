#pragma once

#include <QJsonObject>
#include <QString>

class SensitiveDataSanitizer final
{
public:
    SensitiveDataSanitizer() = delete;

    [[nodiscard]] static QString sanitizeUrl(const QString &url);
    [[nodiscard]] static QString sanitizeText(const QString &text);
    [[nodiscard]] static QJsonObject sanitizeObject(const QJsonObject &object);
};
