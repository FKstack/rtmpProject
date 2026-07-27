#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QThread>

#include <algorithm>

#include "app/StreamConnectionController.h"
#include "app/StyleLoader.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/MainWindow.h"

namespace {

int defaultDecodeWorkerCount()
{
    const int ideal = QThread::idealThreadCount();
    return std::clamp(ideal > 0 ? ideal / 2 : 1, 1, 8);
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RtmpMonitor"));
    QApplication::setOrganizationName(QStringLiteral("RtmpProject"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("最多 16 路 RTMP/H.264 动态实时预览客户端")
    );
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption urlOption(
        {QStringLiteral("u"), QStringLiteral("url")},
        QStringLiteral(
            "启动时预装的 RTMP URL；可重复 1～16 次。不提供时显示空连接页。"
        ),
        QStringLiteral("rtmp-url")
    );
    QCommandLineOption decodeThreadsOption(
        QStringLiteral("decode-threads"),
        QStringLiteral("共享解码 worker 数量，范围 1～16。"),
        QStringLiteral("count"),
        QString::number(defaultDecodeWorkerCount())
    );
    QCommandLineOption metricsFileOption(
        QStringLiteral("metrics-file"),
        QStringLiteral("每秒原子写入 JSON 指标的本地路径。"),
        QStringLiteral("path")
    );
    QCommandLineOption latencyMarkerOption(
        QStringLiteral("latency-marker"),
        QStringLiteral("启用测试画面中的机器可读延迟标记解析。")
    );
    parser.addOption(urlOption);
    parser.addOption(decodeThreadsOption);
    parser.addOption(metricsFileOption);
    parser.addOption(latencyMarkerOption);
    parser.process(app);

    const QStringList streamUrls = parser.values(urlOption);
    if (streamUrls.size() > 16) {
        qCritical().noquote() << QStringLiteral("--url 最多只能重复 16 次。");
        return EXIT_FAILURE;
    }

    bool workerCountValid = false;
    const int workerCount =
        parser.value(decodeThreadsOption).toInt(&workerCountValid);
    if (!workerCountValid || workerCount < 1 || workerCount > 16) {
        qCritical().noquote()
            << QStringLiteral("--decode-threads 必须是 1～16 的整数。");
        return EXIT_FAILURE;
    }

    const StyleLoadResult styleResult =
        StyleLoader::instance().applyApplicationStyle(app);
    if (!styleResult.applied) {
        qWarning().noquote()
            << QStringLiteral("启动时未能应用默认 QSS：%1")
                   .arg(styleResult.errorMessage);
    }

    PlaybackPerformanceOptions performanceOptions;
    performanceOptions.decodeWorkerCount = workerCount;
    performanceOptions.latencyMarkerEnabled =
        parser.isSet(latencyMarkerOption);

    MultiStreamPlaybackManager playbackManager(performanceOptions);
    playbackManager.setMetricsOutputPath(parser.value(metricsFileOption));

    MainWindow mainWindow;
    StreamConnectionController connectionController(
        &mainWindow, &playbackManager
    );

    if (!connectionController.preloadUrls(streamUrls)) {
        qCritical().noquote()
            << QStringLiteral(
                   "预装连接失败：请检查 URL 是否为不重复的有效 rtmp:// 地址。"
               );
        return EXIT_FAILURE;
    }

    mainWindow.show();
    const int exitCode = app.exec();
    playbackManager.stopAll();
    return exitCode;
}
