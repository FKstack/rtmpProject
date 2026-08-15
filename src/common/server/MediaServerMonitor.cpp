#include "server/MediaServerMonitor.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>

#include <algorithm>

MediaServerMonitor::MediaServerMonitor(QObject *parent)
    : QObject(parent)
    , networkManager_(new QNetworkAccessManager(this))
{
    qRegisterMetaType<MediaServerHealth>();

    pollTimer_.setSingleShot(false);
    connect(
        &pollTimer_, &QTimer::timeout,
        this, &MediaServerMonitor::startProbe
    );
    probeTimeoutTimer_.setSingleShot(true);
    connect(
        &probeTimeoutTimer_, &QTimer::timeout,
        this, &MediaServerMonitor::handleProbeTimeout
    );
}

MediaServerMonitor::~MediaServerMonitor()
{
    stopMonitoring();
}

void MediaServerMonitor::setEndpoint(const MediaServerEndpoint &endpoint)
{
    endpoint_ = endpoint;
    // 目标变化后旧的防抖计数没有意义；进行中的探测结果也不得再生效。
    cancelProbe();
    lastServerVersion_.clear();
    health_.serverVersion.clear();
    consecutiveSuccesses_ = 0;
    consecutiveFailures_ = 0;
    consecutiveDegraded_ = 0;
}

void MediaServerMonitor::startMonitoring()
{
    if (monitoring_) {
        return;
    }
    monitoring_ = true;
    consecutiveSuccesses_ = 0;
    consecutiveFailures_ = 0;
    consecutiveDegraded_ = 0;
    tcpProbeOk_ = false;
    apiProbeOk_ = false;
    reportState(
        MediaServerState::Checking,
        QStringLiteral("Media server health check started.")
    );
    probeNow();
    pollTimer_.start(intervalMs_);
}

void MediaServerMonitor::stopMonitoring()
{
    monitoring_ = false;
    pollTimer_.stop();
    cancelProbe();
}

void MediaServerMonitor::probeNow()
{
    if (probeInFlight_) {
        return;
    }
    startProbe();
}

void MediaServerMonitor::setTimingForTesting(
    int intervalMs,
    int probeTimeoutMs,
    int failThreshold,
    int okThreshold
)
{
    intervalMs_ = std::max(1, intervalMs);
    probeTimeoutMs_ = std::max(1, probeTimeoutMs);
    failThreshold_ = std::max(1, failThreshold);
    okThreshold_ = std::max(1, okThreshold);
}

void MediaServerMonitor::startProbe()
{
    if (probeInFlight_) {
        return;
    }
    probeInFlight_ = true;
    tcpProbeDone_ = false;
    tcpProbeOk_ = false;
    tcpDiagnostic_.clear();
    apiProbeDone_ = false;
    apiProbeOk_ = false;
    apiDiagnostic_.clear();

    probeSocket_ = new QTcpSocket(this);
    connect(
        probeSocket_, &QTcpSocket::connected,
        this,
        [this]() {
            finishTcpProbe(true, {});
        }
    );
    connect(
        probeSocket_, &QTcpSocket::errorOccurred,
        this,
        [this](QAbstractSocket::SocketError) {
            finishTcpProbe(
                false,
                probeSocket_ != nullptr
                    ? probeSocket_->errorString()
                    : QStringLiteral("unknown socket error")
            );
        }
    );
    probeSocket_->connectToHost(endpoint_.host, endpoint_.rtmpPort);

    if (endpoint_.apiHealthEnabled) {
        const QNetworkRequest request(apiVersionsUrl());
        probeReply_ = networkManager_->get(request);
        connect(
            probeReply_, &QNetworkReply::finished,
            this,
            [this]() {
                finishApiProbe();
            }
        );
    } else {
        apiProbeDone_ = true;
        apiDiagnostic_ = QStringLiteral("API health check disabled.");
    }

    probeTimeoutTimer_.start(probeTimeoutMs_);
}

void MediaServerMonitor::cancelProbe()
{
    probeTimeoutTimer_.stop();
    // 先复位状态再 abort，abort 同步触发的信号会被完成守卫忽略。
    probeInFlight_ = false;
    tcpProbeDone_ = false;
    apiProbeDone_ = false;
    if (probeSocket_ != nullptr) {
        probeSocket_->abort();
        probeSocket_->deleteLater();
        probeSocket_ = nullptr;
    }
    if (probeReply_ != nullptr) {
        probeReply_->abort();
        probeReply_->deleteLater();
        probeReply_ = nullptr;
    }
}

void MediaServerMonitor::finishTcpProbe(
    bool reachable,
    const QString &diagnostic
)
{
    if (!probeInFlight_ || tcpProbeDone_) {
        return;
    }
    tcpProbeDone_ = true;
    tcpProbeOk_ = reachable;
    tcpDiagnostic_ = diagnostic;
    if (probeSocket_ != nullptr) {
        // 只探测可达性，不做 RTMP 握手；确认后立即断开。
        probeSocket_->abort();
        probeSocket_->deleteLater();
        probeSocket_ = nullptr;
    }
    completeProbeIfReady();
}

void MediaServerMonitor::finishApiProbe()
{
    if (!probeInFlight_ || apiProbeDone_ || probeReply_ == nullptr) {
        return;
    }
    apiProbeDone_ = true;

    QNetworkReply *reply = probeReply_;
    probeReply_ = nullptr;
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QString networkError = reply->errorString();
    const bool networkFailed = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();

    if (networkFailed) {
        apiProbeOk_ = false;
        apiDiagnostic_ = networkError;
    } else if (httpStatus != 200) {
        apiProbeOk_ = false;
        apiDiagnostic_ = QStringLiteral("HTTP status %1.").arg(httpStatus);
    } else {
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
            apiProbeOk_ = false;
            apiDiagnostic_ =
                QStringLiteral("API response is not valid JSON.");
        } else if (document.object()
                       .value(QStringLiteral("code"))
                       .toInt(-1) != 0) {
            apiProbeOk_ = false;
            apiDiagnostic_ =
                QStringLiteral("API reported a non-zero code.");
        } else {
            const QString version = document.object()
                .value(QStringLiteral("data"))
                .toObject()
                .value(QStringLiteral("version"))
                .toString()
                .trimmed();
            if (version.isEmpty()) {
                apiProbeOk_ = false;
                apiDiagnostic_ = QStringLiteral(
                    "API response does not contain an SRS version."
                );
            } else {
                apiProbeOk_ = true;
                lastServerVersion_ = version;
            }
        }
    }
    completeProbeIfReady();
}

void MediaServerMonitor::handleProbeTimeout()
{
    if (!probeInFlight_) {
        return;
    }
    if (!tcpProbeDone_) {
        tcpProbeDone_ = true;
        tcpProbeOk_ = false;
        tcpDiagnostic_ = QStringLiteral("TCP probe timed out.");
        if (probeSocket_ != nullptr) {
            probeSocket_->abort();
            probeSocket_->deleteLater();
            probeSocket_ = nullptr;
        }
    }
    if (!apiProbeDone_) {
        apiProbeDone_ = true;
        apiProbeOk_ = false;
        apiDiagnostic_ = QStringLiteral("API probe timed out.");
        if (probeReply_ != nullptr) {
            probeReply_->abort();
            probeReply_->deleteLater();
            probeReply_ = nullptr;
        }
    }
    completeProbeIfReady();
}

void MediaServerMonitor::completeProbeIfReady()
{
    if (!probeInFlight_ || !tcpProbeDone_ || !apiProbeDone_) {
        return;
    }
    probeInFlight_ = false;
    probeTimeoutTimer_.stop();

    ProbeOutcome outcome = ProbeOutcome::Failure;
    if (!endpoint_.apiHealthEnabled) {
        outcome = tcpProbeOk_ ? ProbeOutcome::Success : ProbeOutcome::Failure;
    } else if (tcpProbeOk_ && apiProbeOk_) {
        outcome = ProbeOutcome::Success;
    } else if (tcpProbeOk_) {
        outcome = ProbeOutcome::Degraded;
    }
    applyOutcome(outcome);
}

void MediaServerMonitor::applyOutcome(ProbeOutcome outcome)
{
    switch (outcome) {
    case ProbeOutcome::Success:
        ++consecutiveSuccesses_;
        consecutiveFailures_ = 0;
        consecutiveDegraded_ = 0;
        if (consecutiveSuccesses_ >= okThreshold_ &&
            health_.state != MediaServerState::Healthy) {
            reportState(
                MediaServerState::Healthy,
                endpoint_.apiHealthEnabled
                    ? QStringLiteral(
                          "Media server RTMP port and API are healthy."
                      )
                    : QStringLiteral(
                          "Media server RTMP port is reachable "
                          "(API check disabled)."
                      )
            );
        }
        break;
    case ProbeOutcome::Degraded:
        ++consecutiveDegraded_;
        consecutiveSuccesses_ = 0;
        consecutiveFailures_ = 0;
        if (consecutiveDegraded_ >= failThreshold_ &&
            health_.state != MediaServerState::Degraded) {
            reportState(
                MediaServerState::Degraded,
                QStringLiteral(
                    "RTMP port reachable, but health API failed: %1"
                ).arg(apiDiagnostic_)
            );
        }
        break;
    case ProbeOutcome::Failure:
        ++consecutiveFailures_;
        consecutiveSuccesses_ = 0;
        consecutiveDegraded_ = 0;
        if (consecutiveFailures_ >= failThreshold_ &&
            health_.state != MediaServerState::Unavailable) {
            reportState(
                MediaServerState::Unavailable,
                QStringLiteral("Media server unreachable: %1")
                    .arg(tcpDiagnostic_)
            );
        }
        break;
    }
}

void MediaServerMonitor::reportState(
    MediaServerState state,
    const QString &diagnostic
)
{
    health_.state = state;
    health_.rtmpPortReachable = tcpProbeOk_;
    health_.apiReachable = apiProbeOk_;
    health_.serverVersion = lastServerVersion_;
    health_.diagnostic = diagnostic;
    emit healthChanged(health_);
}

QUrl MediaServerMonitor::apiVersionsUrl() const
{
    QUrl url = endpoint_.apiBaseUrl;
    url.setPath(QStringLiteral("/api/v1/versions"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}
