#pragma once

#include <QString>

namespace rtmp_monitor::webrtc_client {

enum class WebRtcClientRuntimeLayout {
    Invalid,
    Repository,
    Portable,
};

struct WebRtcClientRuntimePathResolution
{
    WebRtcClientRuntimeLayout layout = WebRtcClientRuntimeLayout::Invalid;
    QString exchangeRoot;
    QString samplePath;

    [[nodiscard]] bool ok() const noexcept
    {
        return layout != WebRtcClientRuntimeLayout::Invalid &&
               !exchangeRoot.isEmpty();
    }
};

/** Pure path policy for repository and manifest-marked portable layouts. */
class WebRtcClientRuntimePaths final
{
public:
    [[nodiscard]] static WebRtcClientRuntimePathResolution resolve(
        const QString &applicationDirectory,
        const QString &currentDirectory
    );
    [[nodiscard]] static QString layoutName(
        WebRtcClientRuntimeLayout layout
    );
};

} // namespace rtmp_monitor::webrtc_client
