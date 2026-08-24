#include "webrtc_client/WebRtcClientOptions.h"

#include <QStringList>

namespace rtmp_monitor::webrtc_client {

void WebRtcClientOptions::configureParser(QCommandLineParser &parser)
{
    parser.setApplicationDescription(
        QStringLiteral("WebRTC V2 publisher/viewer test client")
    );
    parser.addHelpOption();
    parser.addOption({
        QStringLiteral("media-role"),
        QStringLiteral("publisher or viewer"),
        QStringLiteral("role")
    });
    parser.addOption({
        QStringLiteral("signaling-role"),
        QStringLiteral("offer or answer"),
        QStringLiteral("role")
    });
    parser.addOption({
        QStringLiteral("source"),
        QStringLiteral("publisher source; only sample is supported"),
        QStringLiteral("source")
    });
    parser.addOption({
        QStringLiteral("ice-mode"),
        QStringLiteral("host or stun; stun reads the fixed local config"),
        QStringLiteral("mode"),
        QStringLiteral("host")
    });
    parser.addOption({
        QStringLiteral("timeout-ms"),
        QStringLiteral("1000..600000"),
        QStringLiteral("milliseconds"),
        QStringLiteral("30000")
    });
}

std::optional<WebRtcClientOptions> WebRtcClientOptions::fromParser(
    const QCommandLineParser &parser
)
{
    if (!parser.positionalArguments().isEmpty()) return std::nullopt;

    const QString media =
        parser.value(QStringLiteral("media-role")).trimmed().toLower();
    const QString signaling =
        parser.value(QStringLiteral("signaling-role")).trimmed().toLower();
    const QString iceMode =
        parser.value(QStringLiteral("ice-mode")).trimmed().toLower();
    bool timeoutOk = false;
    const int timeoutMs =
        parser.value(QStringLiteral("timeout-ms")).toInt(&timeoutOk);
    if (!timeoutOk || timeoutMs < 1'000 || timeoutMs > 600'000 ||
        !QStringList {QStringLiteral("publisher"), QStringLiteral("viewer")}
             .contains(media) ||
        !QStringList {QStringLiteral("offer"), QStringLiteral("answer")}
             .contains(signaling) ||
        !QStringList {QStringLiteral("host"), QStringLiteral("stun")}
             .contains(iceMode)) {
        return std::nullopt;
    }

    const bool sourceSet = parser.isSet(QStringLiteral("source"));
    const QString source =
        parser.value(QStringLiteral("source")).trimmed().toLower();
    if ((media == QStringLiteral("publisher") &&
         (!sourceSet || source != QStringLiteral("sample"))) ||
        (media == QStringLiteral("viewer") && sourceSet)) {
        return std::nullopt;
    }

    WebRtcClientOptions options;
    options.mediaRole = media == QStringLiteral("publisher")
                            ? ClientMediaRole::Publisher
                            : ClientMediaRole::Viewer;
    options.signalingRole = signaling == QStringLiteral("offer")
                                ? SignalingRole::Offerer
                                : SignalingRole::Answerer;
    options.iceMode = iceMode == QStringLiteral("stun")
                          ? ClientIceMode::Stun
                          : ClientIceMode::HostOnly;
    options.timeout = std::chrono::milliseconds(timeoutMs);
    return options;
}

QString mediaRoleName(ClientMediaRole role)
{
    return role == ClientMediaRole::Publisher
               ? QStringLiteral("publisher")
               : QStringLiteral("viewer");
}

QString signalingRoleName(SignalingRole role)
{
    return role == SignalingRole::Offerer
               ? QStringLiteral("offer")
               : QStringLiteral("answer");
}

QString iceModeName(ClientIceMode mode)
{
    return mode == ClientIceMode::Stun
               ? QStringLiteral("stun")
               : QStringLiteral("host");
}

} // namespace rtmp_monitor::webrtc_client
