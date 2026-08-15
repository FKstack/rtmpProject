#include "server/RtmpUrlBuilder.h"

#include <QRegularExpression>

namespace {

// SRS 官方建议 app 与 stream 使用单层名称；字符集收紧后路径无需再转义，
// 也避免把嵌套路径或空白字符带进 URL。
[[nodiscard]] bool isSingleLayerIdentifier(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\A[A-Za-z0-9_-]+\\z")
    );
    return pattern.match(text).hasMatch();
}

void setError(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
}

} // namespace

std::optional<QUrl> buildRtmpUrl(
    const MediaServerEndpoint &endpoint,
    const QString &streamKey,
    QString *error
)
{
    const QString host = endpoint.host.trimmed();
    if (host.isEmpty()) {
        setError(error, QStringLiteral("媒体服务器地址（host）不能为空。"));
        return std::nullopt;
    }
    // host 只允许主机名或 IP 字面量；空白和路径分隔符不属于合法主机名，
    // 提前拒绝，避免依赖 QUrl 对非法字符的静默转义。
    static const QRegularExpression hostPattern(
        QStringLiteral("\\A[A-Za-z0-9.:\\[\\]%-]+\\z")
    );
    if (!hostPattern.match(host).hasMatch()) {
        setError(
            error,
            QStringLiteral("媒体服务器地址（host）包含非法字符：%1。")
                .arg(host)
        );
        return std::nullopt;
    }
    if (endpoint.rtmpPort == 0) {
        setError(error, QStringLiteral("RTMP 端口必须在 1～65535 之间。"));
        return std::nullopt;
    }
    if (!isSingleLayerIdentifier(endpoint.application)) {
        setError(
            error,
            QStringLiteral(
                "RTMP application 只允许单层字母、数字、下划线和连字符：%1。"
            ).arg(endpoint.application)
        );
        return std::nullopt;
    }
    if (!isSingleLayerIdentifier(streamKey)) {
        setError(
            error,
            QStringLiteral(
                "流名（streamKey）只允许单层字母、数字、下划线和连字符：%1。"
            ).arg(streamKey)
        );
        return std::nullopt;
    }

    QUrl url;
    url.setScheme(QStringLiteral("rtmp"));
    // setHost 接受 IPv6 字面量并在序列化时自动补方括号。
    url.setHost(host);
    url.setPort(endpoint.rtmpPort);
    url.setPath(QStringLiteral("/%1/%2").arg(endpoint.application, streamKey));
    if (!url.isValid()) {
        setError(
            error,
            QStringLiteral("生成的 RTMP URL 非法：%1。")
                .arg(url.errorString())
        );
        return std::nullopt;
    }

    setError(error, {});
    return url;
}
