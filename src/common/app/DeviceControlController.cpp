#include "app/DeviceControlController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMessageBox>

#include <utility>

#include "device_control/MqttDeviceClient.h"
#include "logging/LogManager.h"
#include "ui/DeviceControlPanel.h"
#include "ui/MainWindow.h"
#include "ui/MqttSettingsDialog.h"

DeviceControlController::DeviceControlController(
    MainWindow *mainWindow, DeviceControlPanel *panel, MqttDeviceClient *client,
    LogManager *logManager, MqttSettingsRepository repository, QObject *parent)
    : QObject(parent), mainWindow_(mainWindow), panel_(panel), client_(client),
      logManager_(logManager), repository_(std::move(repository))
{
    Q_ASSERT(mainWindow_ && panel_ && client_ && logManager_);
    connect(panel_, &DeviceControlPanel::commandPressed,
            this, &DeviceControlController::send);
    connect(panel_, &DeviceControlPanel::movementReleased,
            this, &DeviceControlController::stopMovement);
    connect(panel_, &DeviceControlPanel::settingsRequested,
            this, &DeviceControlController::showSettings);
    connect(client_, &MqttDeviceClient::stateChanged, this,
            [this](MqttConnectionState state, const QString &detail) {
                panel_->setConnectionState(state, detail);
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
    connect(client_, &MqttDeviceClient::messageReceived, panel_,
            &DeviceControlPanel::appendObservedMessage);
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
    panel_->setTopic(options_.topic);
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
    if (command == DeviceCommand::StopCar) moving_ = false;
    else if (movement) {
        moving_ = true;
        safetyStopPending_ = false;
    }
    if (!client_->publish(command) && movement) moving_ = false;
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
            [this](const MqttConnectionOptions &candidate) {
                MqttConnectionOptions testOptions = candidate;
                testOptions.enabled = true;
                QString error;
                if (!MqttSettingsRepository::validate(testOptions, &error)) {
                    QMessageBox::warning(mainWindow_, tr("配置无效"), error);
                    return;
                }
                stopMovement();
                panel_->setTopic(testOptions.topic);
                client_->connectToBroker(testOptions);
            });
    if (dialog.exec() != QDialog::Accepted) {
        panel_->setTopic(options_.topic);
        client_->connectToBroker(options_);
        return;
    }
    const MqttConnectionOptions candidate = dialog.options();
    QString error;
    stopMovement();
    client_->disconnectFromBroker();
    if (!repository_.save(candidate, &error)) {
        QMessageBox::warning(mainWindow_, tr("保存失败"), error);
        panel_->setTopic(options_.topic);
        client_->connectToBroker(options_);
        return;
    }
    options_ = candidate;
    panel_->setTopic(options_.topic);
    client_->connectToBroker(options_);
}
