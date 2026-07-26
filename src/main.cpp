#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QPointer>
#include <QVector>

#include "app/StyleLoader.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

namespace {

QStringList defaultStreamUrls()
{
    QStringList urls;
    urls.reserve(MainWindow::kInitialPlaybackWidgetCount);
    for (int cameraNumber = 1;
         cameraNumber <= MainWindow::kInitialPlaybackWidgetCount;
         ++cameraNumber) {
        urls.append(
            QStringLiteral("rtmp://127.0.0.1:1935/live/camera%1")
                .arg(cameraNumber, 3, 10, QLatin1Char('0'))
        );
    }
    return urls;
}

void updateVideoWidgetState(
    VideoWidget *videoWidget,
    FFmpegPlayer::PlaybackState state
)
{
    if (videoWidget == nullptr) {
        return;
    }

    switch (state) {
    case FFmpegPlayer::PlaybackState::Stopped:
        videoWidget->clearFrame();
        videoWidget->setStatusText(QObject::tr("已停止"));
        break;
    case FFmpegPlayer::PlaybackState::Connecting:
        videoWidget->clearFrame();
        videoWidget->setStatusText(QObject::tr("正在连接 RTMP..."));
        break;
    case FFmpegPlayer::PlaybackState::Playing:
        videoWidget->setStatusText(QObject::tr("正在缓冲视频帧..."));
        break;
    case FFmpegPlayer::PlaybackState::Reconnecting:
        videoWidget->clearFrame();
        videoWidget->setStatusText(QObject::tr("连接中断，正在重连..."));
        break;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 为后续配置、日志和系统级 Qt 服务提供稳定的应用与组织标识。
    QApplication::setApplicationName(QStringLiteral("RtmpMonitor"));
    QApplication::setOrganizationName(QStringLiteral("RtmpProject"));

    QCommandLineParser commandLineParser;
    commandLineParser.setApplicationDescription(
        QStringLiteral("四路 RTMP/H.264 实时预览客户端")
    );
    commandLineParser.addHelpOption();
    commandLineParser.addVersionOption();
    QCommandLineOption urlOption(
        {QStringLiteral("u"), QStringLiteral("url")},
        QStringLiteral(
            "要拉取的 RTMP URL；可重复 1～4 次，依次覆盖 Camera 01～04。"
        ),
        QStringLiteral("rtmp-url")
    );
    commandLineParser.addOption(urlOption);
    commandLineParser.process(app);

    QStringList streamUrls = defaultStreamUrls();
    const QStringList urlOverrides = commandLineParser.values(urlOption);
    if (urlOverrides.size() > MainWindow::kInitialPlaybackWidgetCount) {
        qCritical().noquote()
            << QStringLiteral("--url 最多只能重复 %1 次。")
                   .arg(MainWindow::kInitialPlaybackWidgetCount);
        return EXIT_FAILURE;
    }
    for (int index = 0; index < urlOverrides.size(); ++index) {
        streamUrls[index] = urlOverrides.at(index);
    }

    // 启动阶段仅加载一次全局 QSS；外部样式缺失时由 StyleLoader 自动回退到内置资源。
    const StyleLoadResult styleResult = StyleLoader::instance().applyApplicationStyle(app);
    if (!styleResult.applied) {
        qWarning().noquote()
            << QStringLiteral("启动时未能应用默认 QSS：%1").arg(styleResult.errorMessage);
    }

    MainWindow mainWindow;

    QVector<QPointer<VideoWidget>> playbackWidgets;
    playbackWidgets.reserve(MainWindow::kInitialPlaybackWidgetCount);
    for (int index = 0; index < MainWindow::kInitialPlaybackWidgetCount; ++index) {
        VideoWidget *videoWidget = mainWindow.videoWidgetAt(index);
        if (videoWidget == nullptr) {
            qCritical().noquote()
                << QStringLiteral("无法取得 Camera %1 的视频格。").arg(index + 1);
            return EXIT_FAILURE;
        }
        playbackWidgets.append(videoWidget);
    }

    MultiStreamPlaybackManager playbackManager(streamUrls);
    QObject::connect(
        &playbackManager, &MultiStreamPlaybackManager::frameReady,
        &mainWindow,
        [playbackWidgets](int streamIndex, const QImage &image) {
            if (streamIndex >= 0 && streamIndex < playbackWidgets.size() &&
                playbackWidgets.at(streamIndex) != nullptr) {
                playbackWidgets.at(streamIndex)->displayFrame(image);
            }
        }
    );
    QObject::connect(
        &playbackManager, &MultiStreamPlaybackManager::stateChanged,
        &mainWindow,
        [playbackWidgets](
            int streamIndex,
            FFmpegPlayer::PlaybackState state
        ) {
            if (streamIndex >= 0 && streamIndex < playbackWidgets.size()) {
                updateVideoWidgetState(playbackWidgets.at(streamIndex), state);
            }
        }
    );
    QObject::connect(
        &playbackManager, &MultiStreamPlaybackManager::errorOccurred,
        &mainWindow,
        [playbackWidgets](int streamIndex, const QString &message) {
            if (streamIndex >= 0 && streamIndex < playbackWidgets.size() &&
                playbackWidgets.at(streamIndex) != nullptr) {
                playbackWidgets.at(streamIndex)->setStatusText(message);
            }
        }
    );

    mainWindow.show();
    playbackManager.startAll();

    const int exitCode = app.exec();
    playbackManager.stopAll();
    return exitCode;
}
