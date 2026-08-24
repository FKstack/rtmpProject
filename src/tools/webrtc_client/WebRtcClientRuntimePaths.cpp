#include "webrtc_client/WebRtcClientRuntimePaths.h"

#include "webrtc_dev/SessionPackage.h"

#include <QDir>
#include <QFileInfo>

namespace rtmp_monitor::webrtc_client {

using rtmp_monitor::webrtc_dev::SessionPackageStore;

WebRtcClientRuntimePathResolution WebRtcClientRuntimePaths::resolve(
    const QString &applicationDirectory,
    const QString &currentDirectory
)
{
    const QDir applicationDir(applicationDirectory);
    const QString samplePath = applicationDir.absoluteFilePath(
        QStringLiteral("webrtc-assets/sample.mp4")
    );
    if (QFileInfo(applicationDir.absoluteFilePath(
            QStringLiteral("package-manifest.json")
        )).isFile()) {
        return {
            WebRtcClientRuntimeLayout::Portable,
            applicationDir.absoluteFilePath(QStringLiteral("session-exchange")),
            samplePath,
            applicationDir.absoluteFilePath(
                QStringLiteral("local-config/ice-runtime.json")
            )
        };
    }

    QString repositoryRoot =
        SessionPackageStore::discoverRepositoryRoot(currentDirectory);
    if (repositoryRoot.isEmpty()) {
        repositoryRoot = SessionPackageStore::discoverRepositoryRoot(
            applicationDirectory
        );
    }
    if (repositoryRoot.isEmpty()) return {};
    return {
        WebRtcClientRuntimeLayout::Repository,
        SessionPackageStore::exchangeRootForRepository(repositoryRoot),
        samplePath,
        QDir(repositoryRoot).absoluteFilePath(
            QStringLiteral("out/webrtc-p2p/local-config/ice-runtime.json")
        )
    };
}

QString WebRtcClientRuntimePaths::layoutName(WebRtcClientRuntimeLayout layout)
{
    switch (layout) {
    case WebRtcClientRuntimeLayout::Repository:
        return QStringLiteral("repository");
    case WebRtcClientRuntimeLayout::Portable:
        return QStringLiteral("portable");
    case WebRtcClientRuntimeLayout::Invalid: break;
    }
    return QStringLiteral("invalid");
}

} // namespace rtmp_monitor::webrtc_client
