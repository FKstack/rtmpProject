#include "server/MediaServerConfiguration.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

#include <algorithm>
#include <utility>

namespace {

// 与现有 UI/管理器一致的单路数量上限。
constexpr int kMaximumCameraProfileCount = 16;

void addWarning(QStringList *warnings, const QString &message)
{
    if (warnings != nullptr) {
        warnings->append(message);
    }
}

// 与 RtmpUrlBuilder 保持同一字符集；本 Phase 只校验，不生成 URL。
[[nodiscard]] bool isSingleLayerIdentifier(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\A[A-Za-z0-9_-]+\\z")
    );
    return pattern.match(text).hasMatch();
}

[[nodiscard]] bool isValidHost(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\A[A-Za-z0-9.:\\[\\]%-]+\\z")
    );
    return !text.isEmpty() && pattern.match(text).hasMatch();
}

[[nodiscard]] bool parseBool(const QString &text, bool *value)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("true") ||
        normalized == QStringLiteral("1") ||
        normalized == QStringLiteral("yes")) {
        *value = true;
        return true;
    }
    if (normalized == QStringLiteral("false") ||
        normalized == QStringLiteral("0") ||
        normalized == QStringLiteral("no")) {
        *value = false;
        return true;
    }
    return false;
}

} // namespace

MediaServerEndpoint MediaServerConfiguration::loadEndpoint(
    const QString &iniPath,
    QStringList *warnings
)
{
    MediaServerEndpoint endpoint;
    if (iniPath.trimmed().isEmpty() || !QFileInfo::exists(iniPath)) {
        addWarning(
            warnings,
            QStringLiteral(
                "媒体服务器配置文件 %1 不存在，全部使用默认接入点。"
            ).arg(iniPath)
        );
        return endpoint;
    }

    const QSettings settings(iniPath, QSettings::IniFormat);

    // 键缺失时静默使用默认值；只有显式给出空值才记 warning。
    const bool hasHost = settings.contains(QStringLiteral("server/host"));
    const QString host =
        settings.value(QStringLiteral("server/host")).toString().trimmed();
    if (hasHost && !isValidHost(host)) {
        addWarning(
            warnings,
            QStringLiteral("[server] host 非法，回退默认 %1。")
                .arg(endpoint.host)
        );
    } else if (hasHost) {
        endpoint.host = host;
    }

    const QString portText = settings
        .value(QStringLiteral("server/rtmpPort"))
        .toString()
        .trimmed();
    if (!portText.isEmpty()) {
        bool portValid = false;
        const int port = portText.toInt(&portValid);
        if (!portValid || port < 1 || port > 65535) {
            addWarning(
                warnings,
                QStringLiteral(
                    "[server] rtmpPort 值 %1 非法，回退默认 %2。"
                ).arg(portText).arg(endpoint.rtmpPort)
            );
        } else {
            endpoint.rtmpPort = static_cast<quint16>(port);
        }
    }

    const QString application = settings
        .value(QStringLiteral("server/application"))
        .toString()
        .trimmed();
    if (!application.isEmpty()) {
        if (!isSingleLayerIdentifier(application)) {
            addWarning(
                warnings,
                QStringLiteral(
                    "[server] application 值 %1 非法，回退默认 %2。"
                ).arg(application, endpoint.application)
            );
        } else {
            endpoint.application = application;
        }
    }

    const QString apiBaseText = settings
        .value(QStringLiteral("server/apiBaseUrl"))
        .toString()
        .trimmed();
    if (!apiBaseText.isEmpty()) {
        const QUrl apiBaseUrl(apiBaseText);
        const QString scheme = apiBaseUrl.scheme().toLower();
        const bool portRange =
            apiBaseUrl.port() == -1 ||
            (apiBaseUrl.port() >= 1 && apiBaseUrl.port() <= 65535);
        const bool containsCredentials =
            !apiBaseUrl.userName().isEmpty() ||
            !apiBaseUrl.password().isEmpty();
        // 只接受明确的 http/https 绝对地址；相对地址或缺主机都视为非法。
        if (!apiBaseUrl.isValid() ||
            (scheme != QStringLiteral("http") &&
             scheme != QStringLiteral("https")) ||
            apiBaseUrl.host().isEmpty() || !portRange ||
            containsCredentials || apiBaseUrl.hasQuery() ||
            apiBaseUrl.hasFragment()) {
            addWarning(
                warnings,
                QStringLiteral(
                    "[server] apiBaseUrl 非法或包含禁止的凭据/参数，"
                    "回退默认 %1。"
                ).arg(endpoint.apiBaseUrl.toString())
            );
        } else {
            endpoint.apiBaseUrl = apiBaseUrl;
        }
    }

    const QString apiHealthText = settings
        .value(QStringLiteral("server/apiHealthEnabled"))
        .toString()
        .trimmed();
    if (!apiHealthText.isEmpty()) {
        bool enabled = endpoint.apiHealthEnabled;
        if (!parseBool(apiHealthText, &enabled)) {
            addWarning(
                warnings,
                QStringLiteral(
                    "[server] apiHealthEnabled 值 %1 非法，回退默认。"
                ).arg(apiHealthText)
            );
        } else {
            endpoint.apiHealthEnabled = enabled;
        }
    }

    return endpoint;
}

QList<CameraStreamProfile> MediaServerConfiguration::loadCameraProfiles(
    const QString &iniPath,
    QStringList *warnings
)
{
    QList<CameraStreamProfile> profiles;
    if (iniPath.trimmed().isEmpty() || !QFileInfo::exists(iniPath)) {
        addWarning(
            warnings,
            QStringLiteral(
                "媒体服务器配置文件 %1 不存在，未解析到任何摄像头档案。"
            ).arg(iniPath)
        );
        return profiles;
    }

    QSettings settings(iniPath, QSettings::IniFormat);
    QStringList groups = settings.childGroups();
    // 段名排序使超过上限时跳过哪些条目是确定的。
    std::sort(groups.begin(), groups.end());

    QSet<QString> seenCameraIds;
    QSet<QString> seenStreamKeys;
    for (const QString &group : std::as_const(groups)) {
        if (!group.startsWith(
                QStringLiteral("camera"), Qt::CaseInsensitive
            )) {
            continue;
        }

        settings.beginGroup(group);
        CameraStreamProfile profile;
        profile.cameraId = settings
            .value(QStringLiteral("cameraId"), group)
            .toString()
            .trimmed();
        profile.displayName = settings
            .value(QStringLiteral("displayName"))
            .toString()
            .trimmed();
        profile.streamKey = settings
            .value(QStringLiteral("streamKey"))
            .toString()
            .trimmed();
        const QString autoStartText = settings
            .value(QStringLiteral("autoStart"))
            .toString()
            .trimmed();
        settings.endGroup();

        if (profile.displayName.isEmpty()) {
            profile.displayName = profile.cameraId;
        }
        if (!autoStartText.isEmpty()) {
            bool autoStart = profile.autoStart;
            if (!parseBool(autoStartText, &autoStart)) {
                addWarning(
                    warnings,
                    QStringLiteral(
                        "[%1] autoStart 值 %2 非法，回退默认 true。"
                    ).arg(group, autoStartText)
                );
            } else {
                profile.autoStart = autoStart;
            }
        }

        if (!isSingleLayerIdentifier(profile.cameraId)) {
            addWarning(
                warnings,
                QStringLiteral("[%1] cameraId 值 %2 非法，跳过该摄像头。")
                    .arg(group, profile.cameraId)
            );
            continue;
        }
        if (!isSingleLayerIdentifier(profile.streamKey)) {
            addWarning(
                warnings,
                QStringLiteral("[%1] streamKey 值 %2 非法，跳过该摄像头。")
                    .arg(group, profile.streamKey)
            );
            continue;
        }
        if (seenCameraIds.contains(profile.cameraId)) {
            addWarning(
                warnings,
                QStringLiteral(
                    "[%1] cameraId %2 与已有条目重复，跳过该摄像头。"
                ).arg(group, profile.cameraId)
            );
            continue;
        }
        if (seenStreamKeys.contains(profile.streamKey)) {
            addWarning(
                warnings,
                QStringLiteral(
                    "[%1] streamKey %2 与已有条目重复，跳过该摄像头。"
                ).arg(group, profile.streamKey)
            );
            continue;
        }
        if (profiles.size() >= kMaximumCameraProfileCount) {
            addWarning(
                warnings,
                QStringLiteral(
                    "[%1] 摄像头数量超过 %2 路上限，跳过该摄像头。"
                ).arg(group).arg(kMaximumCameraProfileCount)
            );
            continue;
        }

        seenCameraIds.insert(profile.cameraId);
        seenStreamKeys.insert(profile.streamKey);
        profiles.append(profile);
    }

    return profiles;
}
