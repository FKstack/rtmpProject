#include "app/DirectDesktopRuntime.h"

#include "app/PlatformEventBridge.h"
#include "device_control/DeviceControlTypes.h"
#include "mqtt_signaling/DirectRuntimeConfig.h"
#include "mqtt_signaling/MqttSignalingChannel.h"
#include "signaling_session/DirectSessionCore.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

using namespace rtmp::p2p;

namespace {
MqttConnectionState eventState(SignalingChannelState state)
{
    switch (state) {
    case SignalingChannelState::Disabled: return MqttConnectionState::Disabled;
    case SignalingChannelState::Connecting: return MqttConnectionState::Connecting;
    case SignalingChannelState::Subscribing: return MqttConnectionState::Subscribing;
    case SignalingChannelState::Ready: return MqttConnectionState::Connected;
    case SignalingChannelState::Reconnecting: return MqttConnectionState::Reconnecting;
    case SignalingChannelState::Stopped: return MqttConnectionState::Disconnected;
    case SignalingChannelState::Error: return MqttConnectionState::Error;
    }
    return MqttConnectionState::Error;
}

QString sessionStateName(SessionState state)
{
    switch (state) {
    case SessionState::Idle: return QStringLiteral("idle");
    case SessionState::Requested: return QStringLiteral("requested");
    case SessionState::Accepted: return QStringLiteral("accepted");
    case SessionState::Negotiating: return QStringLiteral("negotiating");
    case SessionState::Connected: return QStringLiteral("connected");
    case SessionState::Closing: return QStringLiteral("closing");
    case SessionState::Closed: return QStringLiteral("closed");
    case SessionState::Rejected: return QStringLiteral("rejected");
    case SessionState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("invalid");
}
}

DirectDesktopRuntime::DirectDesktopRuntime(
    QString configPath, QString scenario, QString resultPath,
    PlatformEventBridge *eventBridge, QObject *parent)
    : QObject(parent), configPath_(std::move(configPath)),
      scenario_(std::move(scenario)), resultPath_(std::move(resultPath)),
      eventBridge_(eventBridge)
{
    pollTimer_.setInterval(100);
    connect(&pollTimer_, &QTimer::timeout, this, [this] {
        if (core_) core_->poll();
    });
    timeoutTimer_.setSingleShot(true);
    timeoutTimer_.setInterval(20000);
    connect(&timeoutTimer_, &QTimer::timeout, this,
            [this] { finish(false, QStringLiteral("validation_timeout")); });
}

DirectDesktopRuntime::~DirectDesktopRuntime() = default;

bool DirectDesktopRuntime::start(QString *error)
{
    const DirectRuntimeConfigResult loaded = loadDirectRuntimeConfig(
        configPath_, DirectRuntimeRole::Operator);
    if (!loaded.ok) {
        if (error) *error = loaded.error;
        return false;
    }
    channel_ = std::make_unique<MqttSignalingChannel>(loaded.config);
    core_ = std::make_unique<DirectOperatorCore>(*channel_, loaded.config.identity);
    core_->setChangedHandler([this](const DirectCoreSnapshot &snapshot) {
        handleChanged(snapshot);
    });
    if (!core_->start()) {
        if (error) *error = QStringLiteral("direct_operator_start_failed");
        core_.reset();
        channel_.reset();
        return false;
    }
    pollTimer_.start();
    if (!scenario_.isEmpty()) timeoutTimer_.start();
    return true;
}

void DirectDesktopRuntime::stop()
{
    pollTimer_.stop();
    timeoutTimer_.stop();
    if (core_) core_->stop();
}

void DirectDesktopRuntime::handleChanged(const DirectCoreSnapshot &snapshot)
{
    const int channelStateValue = static_cast<int>(snapshot.channelState);
    if (eventBridge_ && channelStateValue != lastChannelState_)
        eventBridge_->observeMqttSignalingState(eventState(snapshot.channelState));
    lastChannelState_ = channelStateValue;
    if (finished_ || scenario_.isEmpty()) return;
    if (snapshot.channelState == SignalingChannelState::Error) {
        finish(false, QString::fromStdString(snapshot.lastError));
        return;
    }
    if (snapshot.channelState == SignalingChannelState::Ready
        && scenario_ == QStringLiteral("reconnect") && !reconnectStarted_) {
        reconnectStarted_ = true;
        core_->stop();
        QTimer::singleShot(100, this, [this] {
            reconnectCompleted_ = core_ && core_->start();
        });
        return;
    }
    if (snapshot.channelState == SignalingChannelState::Ready
        && (!reconnectStarted_ || reconnectCompleted_) && !requestStarted_) {
        requestStarted_ = true;
        if (!core_->requestStartStream())
            finish(false, QStringLiteral("request_failed"));
        return;
    }
    if (snapshot.sessionState == SessionState::Connected) {
        if (scenario_ == QStringLiteral("duplicate") && !duplicateSent_) {
            duplicateSent_ = true;
            duplicateAwaiting_ = true;
            if (!core_->replayLastCommandForValidation())
                finish(false, QStringLiteral("duplicate_send_failed"));
            else QTimer::singleShot(500, this, [this] {
                if (core_) {
                    duplicateAwaiting_ = false;
                    finish(core_->snapshot().duplicates > 0,
                    core_->snapshot().duplicates > 0
                        ? QString{} : QStringLiteral("duplicate_not_observed"));
                }
            });
            return;
        }
        if (scenario_ == QStringLiteral("duplicate") && duplicateAwaiting_)
            return;
        finish(true);
    }
}

void DirectDesktopRuntime::finish(bool passed, const QString &error)
{
    if (finished_) return;
    finished_ = true;
    pollTimer_.stop();
    timeoutTimer_.stop();
    const DirectCoreSnapshot snapshot = core_ ? core_->snapshot() : DirectCoreSnapshot{};
    if (!resultPath_.isEmpty()) {
        QSaveFile file(resultPath_);
        if (file.open(QIODevice::WriteOnly)) {
            const QJsonObject result{
                {QStringLiteral("schemaVersion"), 1},
                {QStringLiteral("scenario"), scenario_},
                {QStringLiteral("passed"), passed},
                {QStringLiteral("sessionState"), sessionStateName(snapshot.sessionState)},
                {QStringLiteral("received"), static_cast<qint64>(snapshot.received)},
                {QStringLiteral("published"), static_cast<qint64>(snapshot.published)},
                {QStringLiteral("rejected"), static_cast<qint64>(snapshot.rejected)},
                {QStringLiteral("duplicates"), static_cast<qint64>(snapshot.duplicates)},
                {QStringLiteral("errorCode"), error}};
            file.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
            file.commit();
        }
    }
    if (!scenario_.isEmpty())
        QCoreApplication::exit(passed ? EXIT_SUCCESS : EXIT_FAILURE);
}
