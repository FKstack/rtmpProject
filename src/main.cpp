#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>
#include <utility>

#include "app/StreamConnectionController.h"
#include "app/StyleLoader.h"
#include "logging/LogConfiguration.h"
#include "logging/LogManager.h"
#include "logging/UserMessageService.h"
#include "media/MultiStreamPlaybackManager.h"
#include "server/MediaServerConfiguration.h"
#include "server/MediaServerMonitor.h"
#include "ui/MainWindow.h"
#include "ui/VideoCanvasHost.h"

#if defined(Q_OS_LINUX)
#include "linux/LinuxApplicationBootstrap.h"
#include "linux/LinuxRendererFactory.h"
#include "linux/LinuxRenderingPolicy.h"
#endif

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
    QSurfaceFormat surfaceFormat;
    surfaceFormat.setRenderableType(QSurfaceFormat::OpenGL);
    surfaceFormat.setVersion(3, 3);
    surfaceFormat.setProfile(QSurfaceFormat::CoreProfile);
    surfaceFormat.setDepthBufferSize(0);
    surfaceFormat.setStencilBufferSize(0);
    surfaceFormat.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(surfaceFormat);
#elif defined(Q_OS_LINUX)
    // Linux 的 QPA/SurfaceFormat 决策集中在平台 bootstrap：RASTER 构建、
    // 显式 --renderer=cpu 或 linuxfb 目标都不会请求 ES 3.0。
    const LinuxBootstrapResult bootstrapResult =
        LinuxApplicationBootstrap::configureSurfaceFormat(
            LinuxRendererFactory::isOpenGlBackendCompiled(),
            LinuxApplicationBootstrap::requestedRendererFromArgs(argc, argv),
            argc,
            argv
        );
    if (!bootstrapResult.note.isEmpty()) {
        qInfo().noquote() << bootstrapResult.note;
    }
#endif

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
    QCommandLineOption mediaServerConfigOption(
        QStringLiteral("media-server-config"),
        QStringLiteral(
            "媒体服务器 INI 配置路径；缺省时查找程序目录下 media-server.ini。"
        ),
        QStringLiteral("path")
    );
    QCommandLineOption noCameraAutostartOption(
        QStringLiteral("no-camera-autostart"),
        QStringLiteral(
            "只解析校验摄像头档案，禁止启动时自动连接。"
        )
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
    parser.addOption(mediaServerConfigOption);
    parser.addOption(noCameraAutostartOption);
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

#if defined(Q_OS_LINUX)
    // 依据编译能力、CLI 和实际 QPA 名称做 Linux 后端决策；linuxfb 或
    // RASTER 构建直接锁定 CPU，不进入任何 GL 初始化。
    const LinuxRenderingDecision renderingDecision = LinuxRenderingPolicy::decide(
        LinuxRendererFactory::isOpenGlBackendCompiled(),
        rendererName,
        QGuiApplication::platformName()
    );
    if (!renderingDecision.reason.isEmpty()) {
        qWarning().noquote() << renderingDecision.reason;
    }
    rendererPreference = LinuxRendererFactory::rendererPreferenceFor(
        rendererName,
        renderingDecision
    );
#endif

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

    // 媒体服务器只读接入：配置解析失败一律回退默认接入点，
    // 监控全部异步执行，SRS 未运行也不影响启动和其他功能。
    const bool mediaServerConfigExplicitlySet =
        parser.isSet(mediaServerConfigOption);
    QString mediaServerConfigPath =
        parser.value(mediaServerConfigOption).trimmed();
    if (mediaServerConfigPath.isEmpty()) {
        const QString defaultConfigPath =
            QCoreApplication::applicationDirPath() +
            QStringLiteral("/media-server.ini");
        if (QFileInfo::exists(defaultConfigPath)) {
            mediaServerConfigPath = defaultConfigPath;
        }
    }
    MediaServerEndpoint mediaServerEndpoint;
    if (mediaServerConfigPath.isEmpty()) {
        logManager.logSystem(
            LogLevel::Info,
            QStringLiteral("server"),
            QStringLiteral("config_defaulted"),
            QStringLiteral(
                "No media server configuration found; "
                "using the default endpoint."
            )
        );
    } else if (!QFileInfo::exists(mediaServerConfigPath)) {
        logManager.logSystem(
            LogLevel::Warning,
            QStringLiteral("server"),
            QStringLiteral("config_missing"),
            mediaServerConfigExplicitlySet
                ? QStringLiteral(
                      "The explicitly selected media server configuration "
                      "does not exist; using the default endpoint."
                  )
                : QStringLiteral(
                      "Media server configuration does not exist; "
                      "using the default endpoint."
                  ),
            {{QStringLiteral("configFile"), mediaServerConfigPath}}
        );
    } else {
        QStringList mediaServerWarnings;
        mediaServerEndpoint = MediaServerConfiguration::loadEndpoint(
            mediaServerConfigPath,
            &mediaServerWarnings
        );
        logManager.logSystem(
            LogLevel::Info,
            QStringLiteral("server"),
            QStringLiteral("config_loaded"),
            QStringLiteral("Media server configuration loaded."),
            {{QStringLiteral("configFile"), mediaServerConfigPath}}
        );
        for (const QString &warning : std::as_const(mediaServerWarnings)) {
            logManager.logSystem(
                LogLevel::Warning,
                QStringLiteral("server"),
                QStringLiteral("config_warning"),
                warning
            );
        }
    }

    // 摄像头档案只解析校验；是否自动连接由 --no-camera-autostart 与
    // 各 profile 的 autoStart 决定，非法条目已在解析阶段跳过。
    QList<CameraStreamProfile> cameraProfiles;
    if (!mediaServerConfigPath.isEmpty() &&
        QFileInfo::exists(mediaServerConfigPath)) {
        QStringList cameraProfileWarnings;
        cameraProfiles = MediaServerConfiguration::loadCameraProfiles(
            mediaServerConfigPath,
            &cameraProfileWarnings
        );
        for (const QString &warning :
             std::as_const(cameraProfileWarnings)) {
            logManager.logSystem(
                LogLevel::Warning,
                QStringLiteral("server"),
                QStringLiteral("config_warning"),
                warning
            );
        }
    }
    MediaServerMonitor mediaServerMonitor;
    mediaServerMonitor.setEndpoint(mediaServerEndpoint);

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
    connectionController.setMediaServerEndpoint(mediaServerEndpoint);
    QObject::connect(
        &mediaServerMonitor, &MediaServerMonitor::healthChanged,
        &app,
        [&logManager, &userMessageService](
            const MediaServerHealth &health
        ) {
            // SECURITY: 日志字段只含状态与布尔标记，不含完整服务器 URL。
            logManager.logSystem(
                LogLevel::Info,
                QStringLiteral("server"),
                QStringLiteral("health_changed"),
                health.diagnostic,
                {
                    {
                        QStringLiteral("state"),
                        mediaServerStateName(health.state)
                    },
                    {
                        QStringLiteral("rtmpPortReachable"),
                        health.rtmpPortReachable
                    },
                    {
                        QStringLiteral("apiReachable"),
                        health.apiReachable
                    },
                    {
                        QStringLiteral("serverVersion"),
                        health.serverVersion
                    }
                }
            );
            if (health.state == MediaServerState::Unavailable) {
                userMessageService.publish({
                    UserEventType::ServerUnavailable,
                    UserFailureReason::HostUnavailable,
                    0,
                    {}
                });
            } else if (health.state == MediaServerState::Healthy) {
                userMessageService.publish({
                    UserEventType::ServerHealthy,
                    UserFailureReason::None,
                    0,
                    {}
                });
            }
        }
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

    // --url 预装优先；profile 与已装 URL 重复、cameraId 重复或总数
    // 超 16 路时由 addConnection 内部拒绝并记 warning，单路失败
    // 不影响其他 profile 与程序启动。
    int autoStartedProfiles = 0;
    int rejectedProfiles = 0;
    if (!parser.isSet(noCameraAutostartOption)) {
        for (const CameraStreamProfile &profile :
             std::as_const(cameraProfiles)) {
            if (!profile.autoStart) {
                continue;
            }
            if (connectionController.addConnection(
                    profile,
                    mediaServerEndpoint,
                    true
                ) != kInvalidStreamId) {
                ++autoStartedProfiles;
            } else {
                ++rejectedProfiles;
            }
        }
    }
    if (!cameraProfiles.isEmpty()) {
        logManager.logSystem(
            LogLevel::Info,
            QStringLiteral("server"),
            QStringLiteral("camera_profiles_processed"),
            QStringLiteral("Camera profiles processed."),
            {
                {QStringLiteral("profileCount"), cameraProfiles.size()},
                {QStringLiteral("autoStarted"), autoStartedProfiles},
                {QStringLiteral("rejected"), rejectedProfiles},
                {
                    QStringLiteral("autostartDisabled"),
                    parser.isSet(noCameraAutostartOption)
                }
            }
        );
    }

    mainWindow.show();
    mediaServerMonitor.startMonitoring();
    const int exitCode = app.exec();
    mediaServerMonitor.stopMonitoring();
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
