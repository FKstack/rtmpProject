#include "webrtc_product/WebRtcProductTypes.h"

#include <rtc/rtc.hpp>

#include <algorithm>

namespace rtmp_monitor::webrtc_product {
namespace {

bool hasPlaceholderOrWhitespace(const QString &value)
{
    if (value.contains(QLatin1Char('<')) ||
        value.contains(QLatin1Char('>'))) {
        return true;
    }
    return std::any_of(
        value.cbegin(), value.cend(),
        [](QChar character) {
            return character.isSpace() || character.isNull();
        }
    );
}

bool containsCandidateType(
    const std::vector<std::string> &candidateTypes,
    const std::string &expected
)
{
    return std::find(
               candidateTypes.cbegin(), candidateTypes.cend(), expected
           ) != candidateTypes.cend();
}

} // namespace

bool WebRtcProductPolicy::validateRequest(
    const WebRtcSessionRequest &request,
    QString *error
)
{
    const QString displayName = request.displayName.trimmed();
    if (displayName.isEmpty() || displayName.size() > 64) {
        if (error) *error = QStringLiteral("显示名称必须为 1～64 个字符。");
        return false;
    }
    if (request.ice.servers.size() > 1U) {
        if (error) *error = QStringLiteral("一次性会话最多配置一个 STUN 服务。");
        return false;
    }
    for (const IceServerRuntimeConfig &server : request.ice.servers) {
        if (!server.username.empty() || !server.password.empty() ||
            server.urls.size() != 1U) {
            if (error) *error = QStringLiteral("当前一次性入口只接受无凭据的单个 STUN URL。");
            return false;
        }
        const QString url = QString::fromStdString(server.urls.front());
        if (url.isEmpty() || url.size() > 512 ||
            !url.startsWith(QStringLiteral("stun:"), Qt::CaseInsensitive) ||
            hasPlaceholderOrWhitespace(url)) {
            if (error) *error = QStringLiteral("STUN URL 格式无效。");
            return false;
        }
        try {
            const rtc::IceServer parsed(url.toStdString());
            if (parsed.type != rtc::IceServer::Type::Stun) {
                if (error) *error = QStringLiteral("当前一次性入口只接受 STUN。");
                return false;
            }
        } catch (...) {
            if (error) *error = QStringLiteral("STUN URL 格式无效。");
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

bool WebRtcProductPolicy::selectedPairIsNonRelay(
    const rtmp_monitor::webrtc_transport::EndpointConnectionResult &result
) noexcept
{
    if (!result.ok() || !result.selectedPair.has_value()) return false;
    const auto &pair = *result.selectedPair;
    return pair.localType != "relay" && pair.remoteType != "relay" &&
           !pair.localTransport.empty() && !pair.remoteTransport.empty();
}

bool WebRtcProductPolicy::hasFreshDirectEvidence(
    const rtmp_monitor::webrtc_transport::EndpointConnectionResult &result,
    const WebRtcProductDiagnostics &diagnostics,
    qint64 freshnessLimitMs
) noexcept
{
    return freshnessLimitMs >= 0 &&
           selectedPairIsNonRelay(result) &&
           diagnostics.transport.state ==
               rtmp_monitor::webrtc_transport::EndpointState::Connected &&
           diagnostics.media.streamId != kInvalidStreamId &&
           diagnostics.media.presentedFrames > 0 &&
           diagnostics.presentedFrameAgeMs >= 0 &&
           diagnostics.presentedFrameAgeMs <= freshnessLimitMs;
}

WebRtcProductState WebRtcProductPolicy::classifyConnectionFailure(
    rtmp_monitor::webrtc_transport::EndpointError error,
    rtmp_monitor::webrtc_transport::EndpointIceState iceState,
    const std::vector<std::string> &candidateTypes
) noexcept
{
    const bool checksExhausted =
        error == rtmp_monitor::webrtc_transport::EndpointError::ConnectionFailed &&
        iceState == rtmp_monitor::webrtc_transport::EndpointIceState::Failed;
    return checksExhausted && containsCandidateType(candidateTypes, "srflx")
               ? WebRtcProductState::NeedsRelay
               : WebRtcProductState::Error;
}

const char *WebRtcProductPolicy::stateName(WebRtcProductState state) noexcept
{
    switch (state) {
    case WebRtcProductState::Idle: return "idle";
    case WebRtcProductState::Connecting: return "connecting";
    case WebRtcProductState::Direct: return "direct";
    case WebRtcProductState::NeedsRelay: return "needs_relay";
    case WebRtcProductState::Error: return "error";
    }
    return "error";
}

const char *WebRtcProductPolicy::eventName(
    WebRtcProductEventKind kind
) noexcept
{
    switch (kind) {
    case WebRtcProductEventKind::SessionStarted: return "session_started";
    case WebRtcProductEventKind::DescriptionExported:
        return "description_exported";
    case WebRtcProductEventKind::DirectEstablished:
        return "direct_established";
    case WebRtcProductEventKind::MediaInterrupted:
        return "media_interrupted";
    case WebRtcProductEventKind::MediaRecovered: return "media_recovered";
    case WebRtcProductEventKind::NeedsRelay: return "needs_relay";
    case WebRtcProductEventKind::Failed: return "failed";
    case WebRtcProductEventKind::Cancelled: return "cancelled";
    }
    return "failed";
}

} // namespace rtmp_monitor::webrtc_product
