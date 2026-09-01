#include "mqtt_signaling/DirectRuntimeConfig.h"
#include "mqtt_signaling/MqttSignalingChannel.h"
#include "signaling_session/DirectSessionCore.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

using namespace rtmp::p2p;

namespace {
QString deviceStateName(DeviceAgentState state)
{
    switch (state) {
    case DeviceAgentState::Offline: return QStringLiteral("offline");
    case DeviceAgentState::Online: return QStringLiteral("online");
    case DeviceAgentState::Reserved: return QStringLiteral("reserved");
    case DeviceAgentState::Negotiating: return QStringLiteral("negotiating");
    case DeviceAgentState::Streaming: return QStringLiteral("streaming");
    case DeviceAgentState::Closing: return QStringLiteral("closing");
    case DeviceAgentState::Faulted: return QStringLiteral("faulted");
    }
    return QStringLiteral("invalid");
}

bool writeResult(const QString &path, bool passed,
                 const DirectCoreSnapshot &snapshot, const QString &error)
{
    if (path.isEmpty()) return true;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QJsonObject result{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("role"), QStringLiteral("device-harness")},
        {QStringLiteral("passed"), passed},
        {QStringLiteral("deviceState"), deviceStateName(snapshot.deviceState)},
        {QStringLiteral("received"), static_cast<qint64>(snapshot.received)},
        {QStringLiteral("published"), static_cast<qint64>(snapshot.published)},
        {QStringLiteral("rejected"), static_cast<qint64>(snapshot.rejected)},
        {QStringLiteral("duplicates"), static_cast<qint64>(snapshot.duplicates)},
        {QStringLiteral("actions"), static_cast<qint64>(snapshot.actions)},
        {QStringLiteral("errorCode"), error}};
    file.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
    return file.commit();
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("rtmp_monitor_direct_device_harness"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption configOption(QStringLiteral("direct-config"),
        QStringLiteral("Git 外的 device DIRECT 配置。"), QStringLiteral("ignored-json"));
    QCommandLineOption resultOption(QStringLiteral("direct-result"),
        QStringLiteral("脱敏结果路径。"), QStringLiteral("ignored-json"));
    QCommandLineOption timeoutOption(QStringLiteral("timeout-ms"),
        QStringLiteral("有界运行超时。"), QStringLiteral("milliseconds"),
        QStringLiteral("20000"));
    parser.addOption(configOption);
    parser.addOption(resultOption);
    parser.addOption(timeoutOption);
    parser.process(application);
    bool timeoutValid = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutValid);
    const auto loaded = loadDirectRuntimeConfig(
        parser.value(configOption), DirectRuntimeRole::Device);
    if (!loaded.ok || !timeoutValid || timeoutMs < 1000 || timeoutMs > 120000)
        return EXIT_FAILURE;

    MqttSignalingChannel channel(loaded.config);
    DirectDeviceCore core(channel, loaded.config.identity);
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(timeoutMs);
    QObject::connect(&timeout, &QTimer::timeout, &application, [&] {
        const auto snapshot = core.snapshot();
        writeResult(parser.value(resultOption), false, snapshot,
                    QStringLiteral("validation_timeout"));
        core.stop();
        application.exit(EXIT_FAILURE);
    });
    QTimer settle;
    settle.setSingleShot(true);
    settle.setInterval(1500);
    QObject::connect(&settle, &QTimer::timeout, &application, [&] {
        const auto snapshot = core.snapshot();
        const bool passed = snapshot.actions > 0 && snapshot.rejected == 0;
        writeResult(parser.value(resultOption), passed, snapshot,
                    passed ? QString{} : QStringLiteral("device_validation_failed"));
        core.stop();
        application.exit(passed ? EXIT_SUCCESS : EXIT_FAILURE);
    });
    core.setActionHandler([&](DirectAction) {
        if (!settle.isActive()) settle.start();
    });
    if (!core.start()) return EXIT_FAILURE;
    timeout.start();
    return application.exec();
}
