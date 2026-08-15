#include "device_control/MqttSettingsRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <utility>

namespace {
void setError(QString *target, const QString &value)
{
    if (target != nullptr) *target = value;
}
}

MqttSettingsRepository::MqttSettingsRepository(QString filePath)
    : filePath_(filePath.trimmed().isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                QStringLiteral("/mqtt-control.json")
          : std::move(filePath))
{
}

MqttSettingsLoadResult MqttSettingsRepository::load() const
{
    MqttSettingsLoadResult result;
    QFile file(filePath_);
    result.fileExists = file.exists();
    if (!result.fileExists) return result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取 MQTT 配置：%1").arg(file.errorString());
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("MQTT 配置 JSON 无效：%1")
                           .arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (version != kSchemaVersion) {
        result.error = version > kSchemaVersion
            ? QStringLiteral("MQTT 配置版本 %1 高于当前支持的版本 %2。")
                  .arg(version).arg(kSchemaVersion)
            : QStringLiteral("MQTT 配置缺少受支持的 schemaVersion。");
        return result;
    }
    result.options.enabled = root.value(QStringLiteral("enabled")).toBool(false);
    result.options.brokerUrl = root.value(QStringLiteral("brokerUrl")).toString();
    result.options.topic = root.value(QStringLiteral("topic")).toString();
    if (!validate(result.options, &result.error)) result.options = {};
    return result;
}

bool MqttSettingsRepository::save(const MqttConnectionOptions &options,
                                  QString *error) const
{
    if (!validate(options, error)) return false;
    const QFileInfo info(filePath_);
    QDir directory(info.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        setError(error, QStringLiteral("无法创建 MQTT 配置目录。"));
        return false;
    }
    QSaveFile file(filePath_);
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("enabled"), options.enabled},
        {QStringLiteral("brokerUrl"), options.brokerUrl.trimmed()},
        {QStringLiteral("topic"), options.topic.trimmed()}
    });
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(document.toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) {
        setError(error, QStringLiteral("无法原子保存 MQTT 配置：%1")
                            .arg(file.errorString()));
        return false;
    }
    setError(error, {});
    return true;
}

bool MqttSettingsRepository::validate(const MqttConnectionOptions &options,
                                      QString *error)
{
    const QString brokerUrl = options.brokerUrl.trimmed();
    if (brokerUrl.isEmpty() && options.enabled) {
        setError(error, QStringLiteral(
            "启用 MQTT 时必须填写 mqtt://主机[:端口] Broker 地址。"));
        return false;
    }
    if (!brokerUrl.isEmpty()) {
        const QUrl url(brokerUrl, QUrl::StrictMode);
        if (!url.isValid() || url.scheme().compare(QStringLiteral("mqtt"),
                                                   Qt::CaseInsensitive) != 0 ||
            url.host().isEmpty() || !url.userInfo().isEmpty() ||
            !url.path().isEmpty() || !url.query().isEmpty() ||
            !url.fragment().isEmpty()) {
            setError(error, QStringLiteral(
                "Broker 必须是无鉴权的 mqtt://主机[:端口] 地址。"));
            return false;
        }
        const int port = url.port(1883);
        if (port <= 0 || port > 65535) {
            setError(error, QStringLiteral("MQTT Broker 端口无效。"));
            return false;
        }
    }
    const QString topic = options.topic.trimmed();
    if (topic.isEmpty() || topic.size() > 256 || topic.contains(QLatin1Char('#')) ||
        topic.contains(QLatin1Char('+')) || topic.contains(QChar::Null)) {
        setError(error, QStringLiteral("MQTT Topic 不能为空且不得包含通配符。"));
        return false;
    }
    setError(error, {});
    return true;
}

QString MqttSettingsRepository::filePath() const { return filePath_; }
