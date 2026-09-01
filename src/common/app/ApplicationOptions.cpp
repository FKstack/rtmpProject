#include "app/ApplicationOptions.h"

#include <algorithm>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QThread>

#include "RtmpMonitorBuildConfig.h"

namespace {

int defaultDecodeWorkerCount()
{
    const int ideal = QThread::idealThreadCount();
    return std::clamp(ideal > 0 ? ideal / 2 : 1, 1, 8);
}

} // namespace

ApplicationOptionsParseStatus ApplicationOptions::parse(
    QCoreApplication &application,
    ApplicationOptions *options,
    QString *error
)
{
    if (options == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("ApplicationOptions output is required.");
        }
        return ApplicationOptionsParseStatus::Invalid;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("最多 16 路 RTMP/H.264 动态实时预览客户端")
    );
    parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();
    QCommandLineOption urlOption(
        {QStringLiteral("u"), QStringLiteral("url")},
        QStringLiteral("启动时预装的 RTMP URL；可重复 1～16 次。不提供时显示空连接页。"),
        QStringLiteral("rtmp-url")
    );
    QCommandLineOption decodeThreadsOption(
        QStringLiteral("decode-threads"),
        QStringLiteral("共享解码 worker 数量，范围 1～16。"),
        QStringLiteral("count"), QString::number(defaultDecodeWorkerCount())
    );
    QCommandLineOption metricsFileOption(
        QStringLiteral("metrics-file"),
        QStringLiteral("每秒原子写入 JSON 指标的本地路径。"),
        QStringLiteral("path")
    );
    QCommandLineOption rendererOption(
        QStringLiteral("renderer"),
        QStringLiteral("视频渲染后端：auto、opengl 或 cpu。"),
        QStringLiteral("backend"), QStringLiteral("auto")
    );
    QCommandLineOption latencyMarkerOption(
        QStringLiteral("latency-marker"),
        QStringLiteral("启用测试画面中的机器可读延迟标记解析。")
    );
    QCommandLineOption displayFpsOption(
        QStringLiteral("display-fps"),
        QStringLiteral("Display target: auto, 15, 30, or 60 FPS."),
        QStringLiteral("fps"), QStringLiteral("auto")
    );
    QCommandLineOption maximumReconnectFailuresOption(
        QStringLiteral("max-reconnect-failures"),
        QStringLiteral("最大连续失败次数，范围 0～1000；0 表示无限重试。"),
        QStringLiteral("count"), QStringLiteral("0")
    );
    QCommandLineOption logLevelOption(
        QStringLiteral("log-level"),
        QStringLiteral("最低系统日志级别：trace、debug、info、warning、error 或 critical。"),
        QStringLiteral("level")
    );
    QCommandLineOption logDirectoryOption(
        QStringLiteral("log-dir"), QStringLiteral("轮转 JSONL 日志目录。"),
        QStringLiteral("path")
    );
    QCommandLineOption logConfigOption(
        QStringLiteral("log-config"), QStringLiteral("日志 INI 配置文件路径。"),
        QStringLiteral("path")
    );
    QCommandLineOption mediaServerConfigOption(
        QStringLiteral("media-server-config"),
        QStringLiteral("媒体服务器 INI 配置路径；缺省时查找程序目录下 media-server.ini。"),
        QStringLiteral("path")
    );
    QCommandLineOption noCameraAutostartOption(
        QStringLiteral("no-camera-autostart"),
        QStringLiteral("只解析校验摄像头档案，禁止启动时自动连接。")
    );
    QCommandLineOption validationLayoutOption(
        QStringLiteral("validation-layout"),
        QStringLiteral("Hide application chrome for controlled comparison recording.")
    );
#if RTMP_MONITOR_HAS_WEBRTC
    QCommandLineOption directConfigOption(
        QStringLiteral("direct-config"),
        QStringLiteral("Git 外的 DIRECT MQTT 运行配置。"),
        QStringLiteral("ignored-json"));
    QCommandLineOption directScenarioOption(
        QStringLiteral("direct-validation-scenario"),
        QStringLiteral("DIRECT 验证场景：normal、duplicate 或 reconnect。"),
        QStringLiteral("scenario"));
    QCommandLineOption directResultOption(
        QStringLiteral("direct-result"),
        QStringLiteral("写入脱敏 DIRECT 验证结果。"),
        QStringLiteral("ignored-json"));
#endif
    for (const QCommandLineOption &option :
         {urlOption, decodeThreadsOption, metricsFileOption, rendererOption,
          displayFpsOption, latencyMarkerOption, maximumReconnectFailuresOption,
          logLevelOption, logDirectoryOption, logConfigOption,
          mediaServerConfigOption, noCameraAutostartOption,
          validationLayoutOption}) {
        parser.addOption(option);
    }
#if RTMP_MONITOR_HAS_WEBRTC
    parser.addOption(directConfigOption);
    parser.addOption(directScenarioOption);
    parser.addOption(directResultOption);
#endif

    if (!parser.parse(application.arguments())) {
        if (error != nullptr) *error = parser.errorText();
        return ApplicationOptionsParseStatus::Invalid;
    }
    if (parser.isSet(versionOption)) {
        QTextStream output(stdout);
        output << QCoreApplication::applicationName() << ' '
               << QCoreApplication::applicationVersion() << Qt::endl;
        return ApplicationOptionsParseStatus::VersionDisplayed;
    }
    parser.process(application);

    options->streamUrls = parser.values(urlOption);
    if (options->streamUrls.size() > 16) {
        if (error != nullptr) *error = QStringLiteral("--url 最多只能重复 16 次。");
        return ApplicationOptionsParseStatus::Invalid;
    }
    options->rendererName = parser.value(rendererOption).trimmed().toLower();
    if (options->rendererName != QStringLiteral("auto") &&
        options->rendererName != QStringLiteral("opengl") &&
        options->rendererName != QStringLiteral("cpu")) {
        if (error != nullptr) {
            *error = QStringLiteral("--renderer 必须是 auto、opengl 或 cpu。");
        }
        return ApplicationOptionsParseStatus::Invalid;
    }
    bool valid = false;
    options->decodeWorkerCount = parser.value(decodeThreadsOption).toInt(&valid);
    if (!valid || options->decodeWorkerCount < 1 ||
        options->decodeWorkerCount > 16) {
        if (error != nullptr) *error = QStringLiteral("--decode-threads 必须是 1～16 的整数。");
        return ApplicationOptionsParseStatus::Invalid;
    }
    options->maximumReconnectFailures =
        parser.value(maximumReconnectFailuresOption).toInt(&valid);
    if (!valid || options->maximumReconnectFailures < 0 ||
        options->maximumReconnectFailures > 1000) {
        if (error != nullptr) *error = QStringLiteral("--max-reconnect-failures 必须是 0～1000 的整数。");
        return ApplicationOptionsParseStatus::Invalid;
    }
    options->metricsFile = parser.value(metricsFileOption);
    options->displayFps = parser.value(displayFpsOption);
    options->latencyMarkerEnabled = parser.isSet(latencyMarkerOption);
    options->logLevel = parser.value(logLevelOption);
    options->logLevelSet = parser.isSet(logLevelOption);
    options->logDirectory = parser.value(logDirectoryOption);
    options->logDirectorySet = parser.isSet(logDirectoryOption);
    options->logConfig = parser.value(logConfigOption);
    options->mediaServerConfig = parser.value(mediaServerConfigOption);
    options->mediaServerConfigSet = parser.isSet(mediaServerConfigOption);
    options->cameraAutostartDisabled = parser.isSet(noCameraAutostartOption);
    options->validationLayout = parser.isSet(validationLayoutOption);
#if RTMP_MONITOR_HAS_WEBRTC
    options->directConfig = parser.value(directConfigOption).trimmed();
    options->directValidationScenario =
        parser.value(directScenarioOption).trimmed().toLower();
    options->directResult = parser.value(directResultOption).trimmed();
    if ((!options->directValidationScenario.isEmpty()
         || !options->directResult.isEmpty()) && options->directConfig.isEmpty()) {
        if (error) *error = QStringLiteral("DIRECT 验证参数必须同时提供 --direct-config。");
        return ApplicationOptionsParseStatus::Invalid;
    }
    if (!options->directValidationScenario.isEmpty()
        && options->directValidationScenario != QStringLiteral("normal")
        && options->directValidationScenario != QStringLiteral("duplicate")
        && options->directValidationScenario != QStringLiteral("reconnect")) {
        if (error) *error = QStringLiteral(
            "--direct-validation-scenario 必须是 normal、duplicate 或 reconnect。");
        return ApplicationOptionsParseStatus::Invalid;
    }
#endif
    return ApplicationOptionsParseStatus::Ready;
}
