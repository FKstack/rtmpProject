#pragma once

#include <QObject>
#include <QTimer>

#include "server/MediaServerTypes.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTcpSocket;

/**
 * @brief 异步探测媒体服务器健康状态的只读观察器。
 *
 * 每个周期并行执行一次 RTMP 端口 TCP 连接和一次 HTTP API 版本查询，
 * 全部使用 Qt 异步 API；UI 线程不会出现任何阻塞等待。探测结果经防抖
 * 计数后才切换状态，且只在状态变化时发射 healthChanged，避免轮询刷屏。
 * 监控器只观察服务器，不启动、不停止、不重启任何进程。
 *
 * @thread 所有方法必须在对象所在的 GUI/主线程调用，且需要事件循环。
 */
class MediaServerMonitor final : public QObject
{
    Q_OBJECT

public:
    explicit MediaServerMonitor(QObject *parent = nullptr);
    ~MediaServerMonitor() override;

    MediaServerMonitor(const MediaServerMonitor &) = delete;
    MediaServerMonitor &operator=(const MediaServerMonitor &) = delete;

    /**
     * @brief 更新探测目标。进行中的探测会被取消并以新目标重新开始计数。
     */
    void setEndpoint(const MediaServerEndpoint &endpoint);

    /**
     * @brief 开始周期探测：立即进入 Checking 并发起第一次探测。
     */
    void startMonitoring();

    /**
     * @brief 停止周期探测，取消未完成的连接、HTTP 回复和全部定时器；
     * 已报告的状态保持不变。
     */
    void stopMonitoring();

    /**
     * @brief 立即发起一次探测；已有探测进行中时不重复发起。
     * 不要求处于 startMonitoring 状态。
     */
    void probeNow();

    /**
     * @brief 测试专用：覆盖轮询间隔、单次探测超时和防抖阈值。
     * @param intervalMs 轮询间隔。
     * @param probeTimeoutMs 单次探测超时。
     * @param failThreshold 连续失败/降级多少次后进入 Unavailable/Degraded。
     * @param okThreshold 连续成功多少次后进入 Healthy。
     */
    void setTimingForTesting(
        int intervalMs,
        int probeTimeoutMs,
        int failThreshold,
        int okThreshold
    );

signals:
    void healthChanged(const MediaServerHealth &health);

private:
    // 单次探测的分类结果；防抖计数按三类分别累计，任一类出现即清零其他两类。
    enum class ProbeOutcome {
        Success,
        Degraded,
        Failure
    };

    void startProbe();
    void cancelProbe();
    void finishTcpProbe(bool reachable, const QString &diagnostic);
    void finishApiProbe();
    void handleProbeTimeout();
    void completeProbeIfReady();
    void applyOutcome(ProbeOutcome outcome);
    void reportState(MediaServerState state, const QString &diagnostic);
    [[nodiscard]] QUrl apiVersionsUrl() const;

    MediaServerEndpoint endpoint_;
    QNetworkAccessManager *networkManager_ = nullptr;
    QTimer pollTimer_;
    QTimer probeTimeoutTimer_;
    QTcpSocket *probeSocket_ = nullptr;
    QNetworkReply *probeReply_ = nullptr;

    bool monitoring_ = false;
    bool probeInFlight_ = false;
    bool tcpProbeDone_ = false;
    bool tcpProbeOk_ = false;
    QString tcpDiagnostic_;
    bool apiProbeDone_ = false;
    bool apiProbeOk_ = false;
    QString apiDiagnostic_;

    MediaServerHealth health_;
    QString lastServerVersion_;
    int consecutiveSuccesses_ = 0;
    int consecutiveFailures_ = 0;
    int consecutiveDegraded_ = 0;

    int intervalMs_ = 2'000;
    int probeTimeoutMs_ = 1'500;
    int failThreshold_ = 3;
    int okThreshold_ = 2;
};
