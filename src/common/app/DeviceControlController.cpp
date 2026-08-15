#include "app/DeviceControlController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <QElapsedTimer>

#include <utility>

#include "device_control/MqttDeviceClient.h"
#include "device_control/DeviceCommandCodec.h"
#include "device_control/DeviceHeartbeatCodec.h"
#include "device_control/DevicePresenceTracker.h"
#include "logging/LogManager.h"
#include "ui/DeviceControlPanel.h"
#include "ui/MainWindow.h"
#include "ui/MqttSettingsDialog.h"

DeviceControlController::DeviceControlController(
    MainWindow *mainWindow, DeviceControlPanel *panel, MqttDeviceClient *client,
    DevicePresenceTracker *presenceTracker, LogManager *logManager,
    MqttSettingsRepository repository, QObject *parent)
    : QObject(parent), mainWindow_(mainWindow), panel_(panel), client_(client),
      presenceTracker_(presenceTracker), logManager_(logManager),
      repository_(std::move(repository))
{
    Q_ASSERT(mainWindow_ && panel_ && client_ && presenceTracker_ && logManager_);
    connect(panel_, &DeviceControlPanel::commandPressed,
            this, &DeviceControlController::send);
    connect(panel_, &DeviceControlPanel::movementReleased,
            this, &DeviceControlController::stopMovement);
    connect(panel_, &DeviceControlPanel::settingsRequested,
            this, &DeviceControlController::showSettings);
    connect(client_, &MqttDeviceClient::stateChanged, this,
            [this](MqttConnectionState state, const QString &detail) {
                panel_->setConnectionState(state, detail);
                if (state == MqttConnectionState::Connected) {
                    presenceTracker_->setAvailable(true);
                } else if (state == MqttConnectionState::Disabled) {
                    presenceTracker_->setAvailable(false);
                }
                logManager_->logSystem(
                    state == MqttConnectionState::Error
                        ? LogLevel::Warning : LogLevel::Info,
                    QStringLiteral("mqtt"),
                    QStringLiteral("connection_state_changed"),
                    detail.isEmpty() ? QStringLiteral("MQTT state changed.") : detail,
                    {{QStringLiteral("state"), static_cast<int>(state)}},
                    {},
                    state == MqttConnectionState::Reconnecting
                );
                if (state != MqttConnectionState::Connected && moving_) {
                    moving_ = false;
                    safetyStopPending_ = true;
                } else if (state == MqttConnectionState::Connected &&
                           safetyStopPending_) {
                    safetyStopPending_ = false;
                    client_->publish(DeviceCommand::StopCar);
                }
            });
    connect(client_, &MqttDeviceClient::commandSubmitted, this,
            [this](DeviceCommand) { panel_->setLastResult(tr("已提交到 Broker")); });
    connect(client_, &MqttDeviceClient::commandFailed, this,
            [this](DeviceCommand command, const QString &detail) {
                if (command == DeviceCommand::StopCar) safetyStopPending_ = true;
                panel_->setLastResult(detail, true);
                logManager_->logSystem(LogLevel::Warning, QStringLiteral("mqtt"),
                    QStringLiteral("command_failed"), detail);
            });
    connect(client_, &MqttDeviceClient::messageReceived, this,
            &DeviceControlController::handleObservedMessage);
    connect(client_, &MqttDeviceClient::observedMessagesDropped, this,
            [this](quint64 count) {
                panel_->showObservedMessagesDropped(count);
                logManager_->logSystem(
                    LogLevel::Warning, QStringLiteral("mqtt"),
                    QStringLiteral("observed_messages_dropped"),
                    QStringLiteral("MQTT observed message inbox overflowed."),
                    {{QStringLiteral("droppedCount"),
                      static_cast<qint64>(count)}});
            });
    connect(qApp, &QCoreApplication::aboutToQuit,
            this, &DeviceControlController::stop);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) stopMovement();
            });
}

DeviceControlController::~DeviceControlController() { stop(); }

void DeviceControlController::start()
{
    const MqttSettingsLoadResult loaded = repository_.load();
    options_ = loaded.ok() ? loaded.options : MqttConnectionOptions{};
    if (!loaded.ok()) {
        // A corrupt existing file must not silently connect to any remote
        // endpoint. Preserve the file and require the user to repair/save
        // settings explicitly.
        options_.enabled = false;
        panel_->setLastResult(loaded.error, true);
        logManager_->logSystem(LogLevel::Warning, QStringLiteral("mqtt"),
            QStringLiteral("settings_load_failed"), loaded.error);
    }
    panel_->setTopics(options_.topic, options_.statusTopic);
    client_->connectToBroker(options_);
}

void DeviceControlController::stop()
{
    stopMovement();
    client_->disconnectFromBroker();
}

void DeviceControlController::requestSafetyStop()
{
    stopMovement();
}

void DeviceControlController::send(DeviceCommand command)
{
    const bool movement = command == DeviceCommand::MoveForward ||
        command == DeviceCommand::MoveBackward || command == DeviceCommand::TurnLeft ||
        command == DeviceCommand::TurnRight;
    if (targetStreamId_ == kInvalidStreamId || targetDeviceId_.isEmpty()) {
        panel_->setLastResult(tr("请先单击一个视频卡选择控制目标。"), true);
        return;
    }
    if ((command == DeviceCommand::StartStream || movement) &&
        targetPresence_ != DevicePresenceState::Online) {
        panel_->setLastResult(tr("所选设备当前不在线，未发送该指令。"), true);
        return;
    }

    const bool submitted = command == DeviceCommand::StartStream
        ? client_->publishStartStream(targetStreamUrl_)
        : client_->publish(command);
    if (command == DeviceCommand::StopCar) moving_ = false;
    else if (movement && submitted) {
        moving_ = true;
        safetyStopPending_ = false;
    }
}

void DeviceControlController::stopMovement()
{
    if (!moving_) return;
    moving_ = false;
    if (!client_->publish(DeviceCommand::StopCar)) safetyStopPending_ = true;
}

void DeviceControlController::showSettings()
{
    MqttSettingsDialog dialog(mainWindow_);
    dialog.setOptions(options_);
    bool testedDifferentPresenceSession = false;
    connect(client_, &MqttDeviceClient::stateChanged, &dialog,
            [&dialog](MqttConnectionState state, const QString &detail) {
                if (state == MqttConnectionState::Connecting) {
                    dialog.setTestResult(QObject::tr("正在测试连接…"));
                } else if (state == MqttConnectionState::Subscribing) {
                    dialog.setTestResult(QObject::tr("连接成功，正在订阅 Topic…"));
                } else if (state == MqttConnectionState::Connected) {
                    dialog.setTestResult(
                        QObject::tr("连接并订阅成功（未发送设备命令）"));
                } else if (state == MqttConnectionState::Error ||
                           state == MqttConnectionState::Reconnecting) {
                    dialog.setTestResult(detail.isEmpty()
                        ? QObject::tr("连接测试失败") : detail, true);
                }
            });
    connect(&dialog, &MqttSettingsDialog::testRequested, this,
            [this, &testedDifferentPresenceSession](
                const MqttConnectionOptions &candidate) {
                MqttConnectionOptions testOptions = candidate;
                testOptions.enabled = true;
                QString error;
                if (!MqttSettingsRepository::validate(testOptions, &error)) {
                    QMessageBox::warning(mainWindow_, tr("配置无效"), error);
                    return;
                }
                stopMovement();
                const bool sessionChanged =
                    options_.brokerUrl != testOptions.brokerUrl ||
                    options_.statusTopic != testOptions.statusTopic;
                if (sessionChanged) {
                    presenceTracker_->clearSession();
                    testedDifferentPresenceSession = true;
                }
                panel_->setTopics(testOptions.topic, testOptions.statusTopic);
                client_->connectToBroker(testOptions);
            });
    if (dialog.exec() != QDialog::Accepted) {
        if (testedDifferentPresenceSession) presenceTracker_->clearSession();
        panel_->setTopics(options_.topic, options_.statusTopic);
        client_->connectToBroker(options_);
        return;
    }
    const MqttConnectionOptions candidate = dialog.options();
    QString error;
    stopMovement();
    client_->disconnectFromBroker();
    if (!repository_.save(candidate, &error)) {
        QMessageBox::warning(mainWindow_, tr("保存失败"), error);
        panel_->setTopics(options_.topic, options_.statusTopic);
        client_->connectToBroker(options_);
        return;
    }
    const bool sessionChanged = options_.brokerUrl != candidate.brokerUrl ||
        options_.statusTopic != candidate.statusTopic;
    options_ = candidate;
    if (sessionChanged) presenceTracker_->clearSession();
    panel_->setTopics(options_.topic, options_.statusTopic);
    client_->connectToBroker(options_);
}

void DeviceControlController::setControlTarget(
    StreamId streamId, const QString &deviceId, const QString &streamUrl)
{
    if (moving_) stopMovement();
    targetStreamId_ = streamId;
    targetDeviceId_ = deviceId.trimmed();
    targetStreamUrl_ = streamUrl.trimmed();
    targetPresence_ = targetDeviceId_.isEmpty()
        ? DevicePresenceState::Unavailable
        : presenceTracker_->state(targetDeviceId_);
    panel_->setControlTarget(targetDeviceId_, targetPresence_);
}

void DeviceControlController::setDevicePresence(
    const QString &deviceId, DevicePresenceState state)
{
    if (deviceId != targetDeviceId_) return;
    targetPresence_ = state;
    panel_->setDevicePresenceState(state);
    if (state != DevicePresenceState::Online && moving_) stopMovement();
}

void DeviceControlController::handleObservedMessage(
    const MqttObservedMessage &message)
{
    if (message.topic == options_.statusTopic) {
        QString error;
        QElapsedTimer monotonicClock;
        monotonicClock.start();
        const auto heartbeat = DeviceHeartbeatCodec::decode(
            message.payload, monotonicClock.msecsSinceReference(), &error);
        if (heartbeat.has_value()) {
            presenceTracker_->processHeartbeat(*heartbeat);
        } else {
            logManager_->logSystem(
                LogLevel::Warning, QStringLiteral("mqtt"),
                QStringLiteral("invalid_device_heartbeat"), error);
        }
    }
    MqttObservedMessage safe = message;
    if (message.topic == options_.topic) {
        safe.payload = DeviceCommandCodec::redactForDisplay(message.payload);
        safe.originalPayloadSize = safe.payload.size();
    }
    panel_->appendObservedMessage(safe);
}
