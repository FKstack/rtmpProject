#pragma once

#include <QMetaType>
#include <QString>
#include <QUrl>

/**
 * @brief 媒体服务器（SRS）的网络接入点。
 *
 * 只描述“连向哪里”，不包含任何启动、停止或管理服务器进程的能力。
 * 默认值指向开发机本机的 SRS 最小部署（RTMP 1935、HTTP API 1985）。
 */
struct MediaServerEndpoint
{
    QString host = QStringLiteral("127.0.0.1");
    quint16 rtmpPort = 1935;
    QString application = QStringLiteral("live");
    QUrl apiBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:1985"));
    bool apiHealthEnabled = true;
};

/**
 * @brief 单路摄像头的配置档案。
 *
 * 只保存摄像头身份与流名，不重复保存服务器地址；
 * 播放 URL 由 RtmpUrlBuilder 结合 MediaServerEndpoint 生成。
 */
struct CameraStreamProfile
{
    QString cameraId;     // 稳定配置 ID，例如 camera01
    QString displayName;  // 用户可见名称
    QString streamKey;    // SRS stream，例如 camera01
    bool autoStart = true;
};

/**
 * @brief 媒体服务器健康状态。
 *
 * Unknown 表示尚未探测；Checking 表示探测进行中；
 * Healthy 表示 RTMP 端口与 API 均正常；Degraded 表示 RTMP 可达但
 * API 异常（播放不受影响）；Unavailable 表示服务器不可达。
 */
enum class MediaServerState {
    Unknown,
    Checking,
    Healthy,
    Degraded,
    Unavailable
};

/**
 * @brief 一次健康评估的完整结果，跨线程信号传递的负载。
 *
 * serverVersion 记录最近一次 API 成功响应中的版本；
 * diagnostic 为面向日志的简短英文说明，不含 URL 凭据。
 */
struct MediaServerHealth
{
    MediaServerState state = MediaServerState::Unknown;
    bool rtmpPortReachable = false;
    bool apiReachable = false;
    QString serverVersion;
    QString diagnostic;
};

/**
 * @brief 将健康状态转换为日志字段使用的英文名称。
 * @param state 健康状态。
 * @return 稳定的小写状态名，用于结构化日志字段。
 */
[[nodiscard]] inline QString mediaServerStateName(MediaServerState state)
{
    switch (state) {
    case MediaServerState::Unknown:
        return QStringLiteral("unknown");
    case MediaServerState::Checking:
        return QStringLiteral("checking");
    case MediaServerState::Healthy:
        return QStringLiteral("healthy");
    case MediaServerState::Degraded:
        return QStringLiteral("degraded");
    case MediaServerState::Unavailable:
        return QStringLiteral("unavailable");
    }
    return QStringLiteral("unknown");
}

Q_DECLARE_METATYPE(MediaServerHealth)
