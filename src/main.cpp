#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>

#include "app/StreamConnectionController.h"
#include "app/StyleLoader.h"
#include "logging/LogConfiguration.h"
#include "logging/LogManager.h"
#include "logging/UserMessageService.h"
#include "media/MultiStreamPlaybackManager.h"
#include "ui/MainWindow.h"
#include "ui/VideoCanvasHost.h"

namespace {

int defaultDecodeWorkerCount()
{
    const int ideal = QThread::idealThreadCount();
    return std::clamp(ideal > 0 ? ideal / 2 : 1, 1, 8);
}
} // namespace

int main(int argc, char *argv[])
{
#if defined(Q_OS_WIN)
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif
    QSurfaceFormat surfaceFormat;
#if defined(Q_OS_WIN)
    surfaceFormat.setRenderableType(QSurfaceFormat::OpenGL);
    surfaceFormat.setVersion(3, 3);
    surfaceFormat.setProfile(QSurfaceFormat::CoreProfile);
#else
    surfaceFormat.setRenderableType(QSurfaceFormat::OpenGLES);
    surfaceFormat.setVersion(3, 0);
    surfaceFormat.setProfile(QSurfaceFormat::NoProfile);
#endif
    surfaceFormat.setDepthBufferSize(0);
    surfaceFormat.setStencilBufferSize(0);
    surfaceFormat.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(surfaceFormat);

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
    QCommandLineOption rendererOption(
        QStringLiteral("renderer"),
        QStringLiteral("视频渲染后端：auto、opengl 或 cpu。"),
        QStringLiteral("backend"),
        QStringLiteral("auto")
    );
    QCommandLineOption latencyMarkerOption(
        QStringLiteral("latency-marker"),
        QStringLiteral("启用测试画面中的机器可读延迟标记解析。")
    );
    QCommandLineOption maximumReconnectFailuresOption(
        QStringLiteral("max-reconnect-failures"),
        QStringLiteral(
            "最大连续失败次数，范围 0～1000；0 表示无限重试。"
        ),
        QStringLiteral("count"),
        QStringLiteral("0")
    );
    QCommandLineOption logLevelOption(
        QStringLiteral("log-level"),
        QStringLiteral(
            "最低系统日志级别：trace、debug、info、warning、error 或 critical。"
        ),
        QStringLiteral("level")
    );
    QCommandLineOption logDirectoryOption(
        QStringLiteral("log-dir"),
        QStringLiteral("轮转 JSONL 日志目录。"),
        QStringLiteral("path")
    );
    QCommandLineOption logConfigOption(
        QStringLiteral("log-config"),
        QStringLiteral("日志 INI 配置文件路径。"),
        QStringLiteral("path")
    );
    parser.addOption(urlOption);
    parser.addOption(decodeThreadsOption);
    parser.addOption(metricsFileOption);
    parser.addOption(rendererOption);
    parser.addOption(latencyMarkerOption);
    parser.addOption(maximumReconnectFailuresOption);
    parser.addOption(logLevelOption);
    parser.addOption(logDirectoryOption);
    parser.addOption(logConfigOption);
    parser.process(app);

    const QStringList streamUrls = parser.values(urlOption);
    if (streamUrls.size() > 16) {
        qCritical().noquote() << QStringLiteral("--url 最多只能重复 16 次。");
        return EXIT_FAILURE;
    }

    RendererPreference rendererPreference = RendererPreference::Auto;
    const QString rendererName = parser.value(rendererOption).trimmed().toLower();
    if (rendererName == QStringLiteral("opengl")) {
        rendererPreference = RendererPreference::OpenGL;
    } else if (rendererName == QStringLiteral("cpu")) {
        rendererPreference = RendererPreference::Cpu;
    } else if (rendererName != QStringLiteral("auto")) {
        qCritical().noquote()
            << QStringLiteral("--renderer 必须是 auto、opengl 或 cpu。");
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

    bool failureLimitValid = false;
    const int maximumReconnectFailures =
        parser.value(maximumReconnectFailuresOption)
            .toInt(&failureLimitValid);
    if (!failureLimitValid ||
        maximumReconnectFailures < 0 ||
        maximumReconnectFailures > 1'000) {
        qCritical().noquote()
            << QStringLiteral(
                   "--max-reconnect-failures 必须是 0～1000 的整数。"
               );
        return EXIT_FAILURE;
    }

    QString loggingConfigurationError;
    LoggingOptions loggingOptions = LogConfiguration::load(
        parser.value(logConfigOption),
        &loggingConfigurationError
    );
    if (!loggingConfigurationError.isEmpty()) {
        qWarning().noquote() << loggingConfigurationError;
    }
    if (parser.isSet(logLevelOption)) {
        LogLevel minimumLogLevel = loggingOptions.minimumLevel;
        if (!LogManager::parseLevel(
                parser.value(logLevelOption),
                &minimumLogLevel
            )) {
            qCritical().noquote()
                << QStringLiteral(
                       "--log-level 必须是 trace、debug、info、warning、"
                       "error 或 critical。"
                   );
            return EXIT_FAILURE;
        }
        loggingOptions.minimumLevel = minimumLogLevel;
    }
    if (parser.isSet(logDirectoryOption)) {
        loggingOptions.directoryPath = parser.value(logDirectoryOption);
    }
    LogManager logManager;
    const bool logFileReady = logManager.initialize(loggingOptions);
    UserMessageService userMessageService(
        loggingOptions.userMessageRepeatWindowMs
    );

    const StyleLoadResult styleResult =
        StyleLoader::instance().applyApplicationStyle(app);
    if (!styleResult.applied) {
        logManager.logSystem(
            LogLevel::Warning,
            QStringLiteral("ui"),
            QStringLiteral("style_load_failed"),
            styleResult.errorMessage
        );
    }

    PlaybackPerformanceOptions performanceOptions;
    performanceOptions.decodeWorkerCount = workerCount;
    performanceOptions.latencyMarkerEnabled =
        parser.isSet(latencyMarkerOption);
    performanceOptions.maximumConsecutiveFailures =
        maximumReconnectFailures;

    MultiStreamPlaybackManager playbackManager(performanceOptions);
    playbackManager.setMetricsOutputPath(parser.value(metricsFileOption));

    MainWindow mainWindow(rendererPreference);
    playbackManager.setRenderMetricsProvider(
        [&mainWindow] { return mainWindow.rendererRuntimeMetrics(); }
    );
    mainWindow.setUserMessageService(&userMessageService);
    StreamConnectionController connectionController(
        &mainWindow,
        &playbackManager,
        &logManager,
        &userMessageService
    );
    if (!logFileReady) {
        logManager.logSystem(
            LogLevel::Error,
            QStringLiteral("logging"),
            QStringLiteral("initialization_failed"),
            QStringLiteral(
                "One or more local log files could not be initialized."
            )
        );
    }
    logManager.logSystem(
        LogLevel::Info,
        QStringLiteral("application"),
        QStringLiteral("startup"),
        QStringLiteral("RtmpMonitor started."),
        {
            {
                QStringLiteral("reconnectDelayMs"),
                performanceOptions.reconnectDelayMs
            },
            {
                QStringLiteral("maximumReconnectFailures"),
                maximumReconnectFailures
            }
        }
    );

    if (!connectionController.preloadUrls(streamUrls)) {
        qCritical().noquote()
            << QStringLiteral(
                   "预装连接失败：请检查 URL 是否为不重复的有效 rtmp:// 地址。"
               );
        logManager.logSystem(
            LogLevel::Error,
            QStringLiteral("application"),
            QStringLiteral("preload_failed"),
            QStringLiteral("预装连接失败。")
        );
        logManager.shutdown();
        return EXIT_FAILURE;
    }

    mainWindow.show();
    const int exitCode = app.exec();
    playbackManager.stopAll();
    playbackManager.setRenderMetricsProvider({});
    logManager.logSystem(
        LogLevel::Info,
        QStringLiteral("application"),
        QStringLiteral("shutdown"),
        QStringLiteral("RtmpMonitor is shutting down.")
    );
    logManager.shutdown();
    return exitCode;
}
