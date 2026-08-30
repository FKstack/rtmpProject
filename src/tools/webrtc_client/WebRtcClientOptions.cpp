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
        QStringLiteral("publisher source: sample or camera"),
        QStringLiteral("source")
    });
    parser.addOption({
        QStringLiteral("camera-index"),
        QStringLiteral("zero-based camera index for --source=camera"),
        QStringLiteral("index")
    });
    parser.addOption({
        QStringLiteral("list-cameras"),
        QStringLiteral("list runtime camera aliases without opening devices")
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

    const bool listCameras = parser.isSet(QStringLiteral("list-cameras"));
    const QString media =
        parser.value(QStringLiteral("media-role")).trimmed().toLower();
    const QString signaling =
        parser.value(QStringLiteral("signaling-role")).trimmed().toLower();
    const QString iceMode =
        parser.value(QStringLiteral("ice-mode")).trimmed().toLower();
    bool timeoutOk = false;
    const int timeoutMs =
        parser.value(QStringLiteral("timeout-ms")).toInt(&timeoutOk);
    if (listCameras) {
        if (parser.isSet(QStringLiteral("source")) ||
            parser.isSet(QStringLiteral("camera-index")) ||
            parser.isSet(QStringLiteral("media-role")) ||
            parser.isSet(QStringLiteral("signaling-role"))) {
            return std::nullopt;
        }
        WebRtcClientOptions options;
        options.listCameras = true;
        return options;
    }
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
         (!sourceSet ||
          !QStringList {QStringLiteral("sample"), QStringLiteral("camera")}
               .contains(source))) ||
        (media == QStringLiteral("viewer") &&
         (sourceSet || parser.isSet(QStringLiteral("camera-index"))))) {
        return std::nullopt;
    }
    bool cameraOk = false;
    const int cameraIndex = parser.value(QStringLiteral("camera-index"))
        .toInt(&cameraOk);
    if (source == QStringLiteral("camera")) {
        if (!parser.isSet(QStringLiteral("camera-index")) || !cameraOk ||
            cameraIndex < 0 || cameraIndex > 255) {
            return std::nullopt;
        }
    } else if (parser.isSet(QStringLiteral("camera-index"))) {
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
    options.publisherSource = source == QStringLiteral("camera")
        ? ClientPublisherSource::Camera
        : ClientPublisherSource::Sample;
    options.cameraIndex = static_cast<std::uint32_t>(
        source == QStringLiteral("camera") ? cameraIndex : 0
    );
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
