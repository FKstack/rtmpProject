#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QSurfaceFormat>

#include <algorithm>
#include <utility>

#include "app/ApplicationBootstrap.h"
#include "app/ApplicationOptions.h"
#include "app/StreamConnectionController.h"
#include "app/SavedStreamController.h"
#include "app/DeviceControlController.h"
#include "app/DeviceControlTransport.h"
#include "app/PlatformEventBridge.h"
#include "app/StyleLoader.h"
#include "diagnostics/RuntimeMetricsReporter.h"
#include "device_control/MqttDeviceClient.h"
#include "device_control/DevicePresenceTracker.h"
#include "event_center/EventCenterService.h"
#include "logging/LogConfiguration.h"
#include "logging/LogManager.h"
#include "logging/UserMessageService.h"
#include "media/MultiStreamPlaybackManager.h"
#include "render/DisplayFrameRatePolicy.h"
#include "RtmpMonitorBuildConfig.h"
#include "server/MediaServerConfiguration.h"
#include "server/MediaServerMonitor.h"
#include "ui/MainWindow.h"
#include "ui/DeviceControlPanel.h"
#include "ui/DeviceControlInputRouter.h"
#include "ui/EventCenterPanel.h"
#include "ui/VideoCanvasHost.h"

#if defined(Q_OS_LINUX)
#include "linux/LinuxApplicationBootstrap.h"
#include "linux/LinuxRendererFactory.h"
#include "linux/LinuxRenderingPolicy.h"
#endif

#if defined(Q_OS_WIN)
#include "windows/WindowsWindowAppearance.h"
#endif

int ApplicationBootstrap::run(int argc, char *argv[])
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
    QApplication::setApplicationDisplayName(QStringLiteral("RtmpMonitor 监控台"));
    QApplication::setApplicationVersion(
        QStringLiteral(RTMP_MONITOR_VERSION_STRING)
    );
    QApplication::setOrganizationName(QStringLiteral("RtmpProject"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/rtmp-monitor-256.png")));

#if defined(Q_OS_WIN)
    WindowsWindowAppearance windowAppearance(app);
#endif

    ApplicationOptions options;
    QString optionsError;
    const ApplicationOptionsParseStatus optionsStatus =
        ApplicationOptions::parse(app, &options, &optionsError);
    if (optionsStatus == ApplicationOptionsParseStatus::VersionDisplayed) {
        return EXIT_SUCCESS;
    }
    if (optionsStatus == ApplicationOptionsParseStatus::Invalid) {
        qCritical().noquote() << optionsError;
        return EXIT_FAILURE;
    }

    const auto displayFpsRequest = DisplayFrameRatePolicy::parse(
        options.displayFps
    );
    if (!displayFpsRequest.has_value()) {
        qCritical().noquote()
            << QStringLiteral("--display-fps must be auto, 15, 30, or 60.");
        return EXIT_FAILURE;
    }
#if defined(Q_OS_WIN)
    constexpr DisplayFrameRatePlatform displayPlatform =
        DisplayFrameRatePlatform::Windows;
#else
    constexpr DisplayFrameRatePlatform displayPlatform =
        DisplayFrameRatePlatform::LinuxArm64;
#endif
    const DisplayFrameRateDecision displayFpsDecision =
        DisplayFrameRatePolicy::decide(
            *displayFpsRequest,
            displayPlatform,
            std::max(1, static_cast<int>(options.streamUrls.size()))
        );

    RendererPreference rendererPreference = RendererPreference::Auto;
    const QString rendererName = options.rendererName;
    if (rendererName == QStringLiteral("opengl")) {
        rendererPreference = RendererPreference::OpenGL;
    } else if (rendererName == QStringLiteral("cpu")) {
        rendererPreference = RendererPreference::Cpu;
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

    const int workerCount = options.decodeWorkerCount;
    const int maximumReconnectFailures = options.maximumReconnectFailures;

    QString loggingConfigurationError;
    LoggingOptions loggingOptions = LogConfiguration::load(
        options.logConfig,
        &loggingConfigurationError
    );
    if (!loggingConfigurationError.isEmpty()) {
        qWarning().noquote() << loggingConfigurationError;
    }
    if (options.logLevelSet) {
        LogLevel minimumLogLevel = loggingOptions.minimumLevel;
        if (!LogManager::parseLevel(
                options.logLevel,
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
    if (options.logDirectorySet) {
        loggingOptions.directoryPath = options.logDirectory;
    }
    LogManager logManager;
    const bool logFileReady = logManager.initialize(loggingOptions);
    UserMessageService userMessageService(
        loggingOptions.userMessageRepeatWindowMs
    );
    EventCenterService eventCenterService;
    QString eventCenterError;
    const bool eventCenterWriteReady =
        eventCenterService.initialize(&eventCenterError);
    if (!eventCenterWriteReady) {
        logManager.logSystem(
            LogLevel::Critical,
            QStringLiteral("event_center"),
            QStringLiteral("storage_unavailable"),
            eventCenterError,
            {{QStringLiteral("playbackAffected"), false},
             {QStringLiteral("controlAffected"), false}}
        );
    }

    // 媒体服务器只读接入：配置解析失败一律回退默认接入点，
    // 监控全部异步执行，SRS 未运行也不影响启动和其他功能。
    const bool mediaServerConfigExplicitlySet =
        options.mediaServerConfigSet;
    QString mediaServerConfigPath =
        options.mediaServerConfig.trimmed();
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
        options.latencyMarkerEnabled;
    performanceOptions.maximumConsecutiveFailures =
        maximumReconnectFailures;

    MultiStreamPlaybackManager playbackManager(performanceOptions);

    MainWindow mainWindow(rendererPreference);
    mainWindow.setDisplayFrameRateRequest(
        displayFpsDecision.requestedName,
        displayFpsDecision.effectiveFps
    );
    RuntimeMetricsReporter metricsReporter(&playbackManager);
    metricsReporter.setOutputPath(options.metricsFile);
    metricsReporter.setRenderMetricsProvider(
        [&mainWindow] { return mainWindow.rendererRuntimeMetrics(); }
    );
    mainWindow.setUserMessageService(&userMessageService);
    auto *eventCenterPanel = new EventCenterPanel(&mainWindow);
    mainWindow.installEventCenterPanel(eventCenterPanel);
    eventCenterPanel->setEvents(
        eventCenterService.events(), eventCenterService.summary());
    eventCenterPanel->setStorageState(
        eventCenterService.isWriteEnabled(), eventCenterService.storageError());
    mainWindow.setEventCenterSummary(
        eventCenterService.summary(), eventCenterService.isWriteEnabled());
    QObject::connect(
        &eventCenterService, &EventCenterService::eventsChanged,
        eventCenterPanel, &EventCenterPanel::setEvents);
    QObject::connect(
        &eventCenterService, &EventCenterService::eventsChanged,
        &mainWindow,
        [&mainWindow, &eventCenterService](
            const QList<SecurityEventRecord> &,
            const EventCenterSummary &summary) {
            mainWindow.setEventCenterSummary(
                summary, eventCenterService.isWriteEnabled());
        });
    QObject::connect(
        &eventCenterService, &EventCenterService::storageStateChanged,
        eventCenterPanel, &EventCenterPanel::setStorageState);
    QObject::connect(
        &eventCenterService, &EventCenterService::storageStateChanged,
        &mainWindow,
        [&mainWindow, &eventCenterService](bool writeEnabled,
                                           const QString &) {
            mainWindow.setEventCenterSummary(
                eventCenterService.summary(), writeEnabled);
        });
    StreamConnectionController connectionController(
        &mainWindow,
        &playbackManager,
        &logManager,
        &userMessageService
    );
    SavedStreamController savedStreamController(
        &mainWindow, &connectionController);
    QString savedStreamsError;
    if (!savedStreamController.load(&savedStreamsError)) {
        logManager.logSystem(
            LogLevel::Warning,
            QStringLiteral("profiles"),
            QStringLiteral("saved_streams_load_failed"),
            savedStreamsError
        );
        QMessageBox::warning(
            &mainWindow,
            QObject::tr("保存的推流未加载"),
            savedStreamsError
        );
    }
    QObject::connect(&mainWindow, &MainWindow::savedStreamsRequested,
                     &savedStreamController, &SavedStreamController::showDialog);

    auto *deviceControlPanel = new DeviceControlPanel(&mainWindow);
    mainWindow.installDeviceControlPanel(deviceControlPanel);
    DeviceControlInputRouter deviceControlInput(&mainWindow);
    MqttDeviceClient mqttClient;
    MqttDeviceControlTransportAdapter deviceControlTransport(&mqttClient);
    DevicePresenceTracker devicePresenceTracker;
    QObject::connect(deviceControlPanel,
                     &DeviceControlPanel::keyboardModeSelected,
                     &deviceControlInput,
                     &DeviceControlInputRouter::setKeyboardModeSelected);
    QObject::connect(deviceControlPanel,
                     &DeviceControlPanel::inputResetRequested,
                     &deviceControlInput,
                     &DeviceControlInputRouter::clearMovementState);
    QObject::connect(deviceControlPanel,
                     &DeviceControlPanel::controlContextLost,
                     &deviceControlInput,
                     &DeviceControlInputRouter::cancelAndDisarm);
    QObject::connect(&deviceControlInput,
                     &DeviceControlInputRouter::directionKeyStateChanged,
                     deviceControlPanel,
                     &DeviceControlPanel::setKeyboardDirectionState);
    DeviceControlController deviceControlController(
        &mainWindow, deviceControlPanel, &deviceControlTransport,
        &devicePresenceTracker, &logManager,
        [&connectionController](StreamId streamId) {
            return connectionController.controlMediaObservation(streamId);
        });
    PlatformEventBridge platformEventBridge(&eventCenterService);
    platformEventBridge.setMediaServerEndpoint(mediaServerEndpoint);
    eventCenterPanel->setResources(platformEventBridge.resources());
    QObject::connect(
        &platformEventBridge, &PlatformEventBridge::resourcesChanged,
        eventCenterPanel, &EventCenterPanel::setResources);
    const auto showEventOperationFailure =
        [eventCenterPanel](const EventOperationResult &result) {
            if (!result.succeeded())
                eventCenterPanel->showOperationError(result.message);
        };
    QObject::connect(
        eventCenterPanel, &EventCenterPanel::manualIncidentRequested,
        &eventCenterService,
        [&eventCenterService, showEventOperationFailure](
            SecurityEventSeverity severity,
            const QString &resourceId,
            const QString &deviceId,
            const QString &displayName,
            const QString &identitySource,
            const QString &note) {
            showEventOperationFailure(eventCenterService.createManualIncident(
                severity, resourceId, deviceId, displayName, identitySource,
                note, PlatformEventBridge::localActorName()));
        });
    QObject::connect(
        eventCenterPanel, &EventCenterPanel::acknowledgeRequested,
        &eventCenterService,
        [&eventCenterService, showEventOperationFailure](const QString &eventId) {
            showEventOperationFailure(eventCenterService.acknowledge(
                eventId, PlatformEventBridge::localActorName()));
        });
    QObject::connect(
        eventCenterPanel, &EventCenterPanel::resolveManualRequested,
        &eventCenterService,
        [&eventCenterService, showEventOperationFailure](const QString &eventId) {
            showEventOperationFailure(eventCenterService.resolveManualIncident(
                eventId, PlatformEventBridge::localActorName()));
        });
    QObject::connect(
        eventCenterPanel, &EventCenterPanel::closeRequested,
        &eventCenterService,
        [&eventCenterService, showEventOperationFailure](const QString &eventId) {
            showEventOperationFailure(eventCenterService.closeResolved(
                eventId, PlatformEventBridge::localActorName()));
        });
    QObject::connect(
        eventCenterPanel, &EventCenterPanel::forceCloseRequested,
        &eventCenterService,
        [&eventCenterService, showEventOperationFailure](
            const QString &eventId, const QString &reason) {
            showEventOperationFailure(
                eventCenterService.closeWithoutObservedRecovery(
                    eventId, reason, PlatformEventBridge::localActorName()));
        });
    QObject::connect(deviceControlPanel,
                     &DeviceControlPanel::joystickCommandPressed,
                     &deviceControlController,
                     [&deviceControlController](DeviceCommand command) {
                         deviceControlController.submitCommand(
                             command, ControlAttemptSource::Joystick);
                     });
    QObject::connect(deviceControlPanel,
                     &DeviceControlPanel::joystickMovementReleased,
                     &deviceControlController,
                     [&deviceControlController] {
                         deviceControlController.releaseMovement(
                             ControlAttemptSource::Joystick);
                     });
    QObject::connect(deviceControlPanel,
                     &DeviceControlPanel::buttonCommandPressed,
                     &deviceControlController,
                     [&deviceControlController](DeviceCommand command) {
                         deviceControlController.submitCommand(
                             command, ControlAttemptSource::Button);
                     });
    QObject::connect(deviceControlPanel,
                     &DeviceControlPanel::controlArmRequested,
                     &deviceControlController,
                     &DeviceControlController::setControlArmed);
    QObject::connect(deviceControlPanel,
                     &DeviceControlPanel::controlContextLost,
                     &deviceControlController,
                     [&deviceControlController] {
                         deviceControlController.invalidateControl(
                             ControlInvalidationCause::FocusLost,
                             ControlAttemptSource::FocusLost);
                     });
    QObject::connect(&connectionController,
                     &StreamConnectionController::deviceBound,
                     &devicePresenceTracker,
                     &DevicePresenceTracker::registerDevice);
    QObject::connect(&connectionController,
                     &StreamConnectionController::deviceBound,
                     &platformEventBridge,
                     &PlatformEventBridge::observeDeviceBound);
    QObject::connect(&connectionController,
                     &StreamConnectionController::deviceUnbound,
                     &devicePresenceTracker,
                     &DevicePresenceTracker::unregisterDevice);
    QObject::connect(&connectionController,
                     &StreamConnectionController::deviceUnbound,
                     &platformEventBridge,
                     &PlatformEventBridge::observeDeviceUnbound);
    QObject::connect(&connectionController,
                     &StreamConnectionController::streamEventObserved,
                     &platformEventBridge,
                     &PlatformEventBridge::observeStream);
    QObject::connect(&connectionController,
                     &StreamConnectionController::streamRemovedObserved,
                     &platformEventBridge,
                     &PlatformEventBridge::observeStreamRemoved);
    QObject::connect(&connectionController,
                     &StreamConnectionController::controlTargetChanged,
                     &deviceControlController,
                     &DeviceControlController::setControlTarget);
    QObject::connect(&connectionController,
                     &StreamConnectionController::controlTargetMediaChanged,
                     &deviceControlController,
                     [&deviceControlController](StreamId) {
                         deviceControlController.refreshControlAvailability();
                     });
    QObject::connect(&devicePresenceTracker,
                     &DevicePresenceTracker::presenceChanged,
                     &connectionController,
                     &StreamConnectionController::setDevicePresence);
    QObject::connect(&devicePresenceTracker,
                     &DevicePresenceTracker::presenceChanged,
                     &deviceControlController,
                     &DeviceControlController::setDevicePresence);
    QObject::connect(&devicePresenceTracker,
                     &DevicePresenceTracker::presenceChanged,
                     &platformEventBridge,
                     &PlatformEventBridge::observePresence);
    QObject::connect(&deviceControlInput,
                     &DeviceControlInputRouter::commandPressed,
                     &deviceControlController,
                     [&deviceControlController](DeviceCommand command) {
                         deviceControlController.submitCommand(
                             command, ControlAttemptSource::Keyboard);
                     });
    QObject::connect(&deviceControlInput,
                     &DeviceControlInputRouter::movementReleased,
                     &deviceControlController,
                     [&deviceControlController] {
                         deviceControlController.releaseMovement(
                             ControlAttemptSource::Keyboard);
                     });
    QObject::connect(&deviceControlTransport,
                     &DeviceControlTransport::stateChanged,
                     &deviceControlInput,
                     [&deviceControlInput](MqttConnectionState state,
                                           const QString &) {
                         deviceControlInput.setConnected(
                             state == MqttConnectionState::Connected);
                     });
    QObject::connect(
        &deviceControlTransport, &DeviceControlTransport::stateChanged,
        &platformEventBridge,
        [&platformEventBridge](MqttConnectionState state, const QString &) {
            platformEventBridge.observeMqttState(state);
        });
    QObject::connect(
        &deviceControlController,
        &DeviceControlController::controlAttemptRecorded,
        &platformEventBridge,
        &PlatformEventBridge::observeControlAttempt);
    QObject::connect(&deviceControlController,
                     &DeviceControlController::controlSessionChanged,
                     &deviceControlInput,
                     [&deviceControlInput](bool armed, bool, const QString &) {
                         deviceControlInput.setKeyboardArmed(armed);
                     });
    QObject::connect(&deviceControlController,
                     &DeviceControlController::interactiveControlRevoked,
                     deviceControlPanel,
                     &DeviceControlPanel::cancelInteractiveControl);
    QObject::connect(&deviceControlController,
                     &DeviceControlController::interactiveControlRevoked,
                     &deviceControlInput,
                     &DeviceControlInputRouter::cancelAndDisarm);
    QObject::connect(&deviceControlInput,
                     &DeviceControlInputRouter::explicitLockRequested,
                     &deviceControlController,
                     [&deviceControlController] {
                         deviceControlController.setControlArmed(false);
                     });
    QObject::connect(&deviceControlInput,
                     &DeviceControlInputRouter::controlContextLost,
                     &deviceControlController,
                     [&deviceControlController] {
                         deviceControlController.invalidateControl(
                             ControlInvalidationCause::FocusLost,
                             ControlAttemptSource::FocusLost);
                     });
    QObject::connect(&mainWindow, &MainWindow::fullscreenTransitionStarted,
                     &deviceControlController,
                     [&deviceControlController] {
                         deviceControlController.invalidateControl(
                             ControlInvalidationCause::FullscreenTransition,
                             ControlAttemptSource::FullscreenTransition);
                     });
    QObject::connect(&mainWindow, &MainWindow::fullscreenTransitionStarted,
                     deviceControlPanel,
                     &DeviceControlPanel::cancelInteractiveControl);
    QObject::connect(&mainWindow, &MainWindow::fullscreenTransitionStarted,
                     &deviceControlInput,
                     &DeviceControlInputRouter::cancelAndDisarm);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit,
                     deviceControlPanel,
                     &DeviceControlPanel::cancelInteractiveControl);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit,
                     &deviceControlInput,
                     &DeviceControlInputRouter::cancelAndDisarm);
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
    QObject::connect(
        &mediaServerMonitor, &MediaServerMonitor::healthChanged,
        &platformEventBridge, &PlatformEventBridge::observeMediaHealth);
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

    if (!connectionController.preloadUrls(options.streamUrls)) {
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
    if (!options.cameraAutostartDisabled) {
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
                    options.cameraAutostartDisabled
                }
            }
        );
    }

    mainWindow.show();
    if (options.validationLayout) {
        mainWindow.setValidationLayoutMode(true);
    }

    // Keep the existing --url -> deployment profile order; user-saved
    // auto-connect entries are appended and never displace an earlier stream.
    savedStreamController.autoConnect();
    mediaServerMonitor.startMonitoring();
    deviceControlController.start();
    const int exitCode = app.exec();
    platformEventBridge.beginShutdown();
    deviceControlController.stop();
    platformEventBridge.stopAccepting();
    mediaServerMonitor.stopMonitoring();
    playbackManager.stopAll();
    metricsReporter.setRenderMetricsProvider({});
    logManager.logSystem(
        LogLevel::Info,
        QStringLiteral("application"),
        QStringLiteral("shutdown"),
        QStringLiteral("RtmpMonitor is shutting down.")
    );
    logManager.shutdown();
    return exitCode;
}
