#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>

#include "app/StyleLoader.h"
#include "media/FFmpegPlayer.h"
#include "ui/MainWindow.h"
#include "ui/VideoWidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 为后续配置、日志和系统级 Qt 服务提供稳定的应用与组织标识。
    QApplication::setApplicationName(QStringLiteral("RtmpMonitor"));
    QApplication::setOrganizationName(QStringLiteral("RtmpProject"));

    QCommandLineParser commandLineParser;
    commandLineParser.setApplicationDescription(
        QStringLiteral("一路 RTMP/H.264 实时预览客户端")
    );
    commandLineParser.addHelpOption();
    commandLineParser.addVersionOption();
    QCommandLineOption urlOption(
        {QStringLiteral("u"), QStringLiteral("url")},
        QStringLiteral("要拉取的 RTMP URL。"),
        QStringLiteral("rtmp-url"),
        QStringLiteral("rtmp://127.0.0.1:1935/live/camera001")
    );
    commandLineParser.addOption(urlOption);
    commandLineParser.process(app);

    // 启动阶段仅加载一次全局 QSS；外部样式缺失时由 StyleLoader 自动回退到内置资源。
    const StyleLoadResult styleResult = StyleLoader::instance().applyApplicationStyle(app);
    if (!styleResult.applied) {
        qWarning().noquote()
            << QStringLiteral("启动时未能应用默认 QSS：%1").arg(styleResult.errorMessage);
    }

    MainWindow mainWindow;
    FFmpegPlayer player;
    VideoWidget *primaryVideoWidget = mainWindow.primaryVideoWidget();

    if (primaryVideoWidget != nullptr) {
        QObject::connect(
            &player, &FFmpegPlayer::frameReady,
            primaryVideoWidget, &VideoWidget::displayFrame
        );
        QObject::connect(
            &player, &FFmpegPlayer::stateChanged,
            primaryVideoWidget,
            [primaryVideoWidget](FFmpegPlayer::PlaybackState state) {
                switch (state) {
                case FFmpegPlayer::PlaybackState::Stopped:
                    primaryVideoWidget->clearFrame();
                    primaryVideoWidget->setStatusText(QObject::tr("已停止"));
                    break;
                case FFmpegPlayer::PlaybackState::Connecting:
                    primaryVideoWidget->clearFrame();
                    primaryVideoWidget->setStatusText(QObject::tr("正在连接 RTMP..."));
                    break;
                case FFmpegPlayer::PlaybackState::Playing:
                    primaryVideoWidget->setStatusText(QObject::tr("正在缓冲视频帧..."));
                    break;
                case FFmpegPlayer::PlaybackState::Reconnecting:
                    primaryVideoWidget->clearFrame();
                    primaryVideoWidget->setStatusText(QObject::tr("连接中断，正在重连..."));
                    break;
                }
            }
        );
        QObject::connect(
            &player, &FFmpegPlayer::errorOccurred,
            primaryVideoWidget,
            [primaryVideoWidget](const QString &message) {
                primaryVideoWidget->setStatusText(message);
            }
        );
    }

    mainWindow.show();

    if (primaryVideoWidget != nullptr) {
        player.start(commandLineParser.value(urlOption));
    }

    const int exitCode = app.exec();
    player.stop();
    return exitCode;
}
