#pragma once

#include <QObject>

#include "device_control/DeviceControlTypes.h"
#include "device_control/MqttSettingsRepository.h"

class DeviceControlPanel;
class LogManager;
class MainWindow;
class MqttDeviceClient;

class DeviceControlController final : public QObject
{
    Q_OBJECT
public:
    DeviceControlController(MainWindow *mainWindow,
                            DeviceControlPanel *panel,
                            MqttDeviceClient *client,
                            LogManager *logManager,
                            MqttSettingsRepository repository = MqttSettingsRepository(),
                            QObject *parent = nullptr);
    ~DeviceControlController() override;

    void start();
    void stop();
    void requestSafetyStop();

private:
    void send(DeviceCommand command);
    void stopMovement();
    void showSettings();

    MainWindow *mainWindow_ = nullptr;
    DeviceControlPanel *panel_ = nullptr;
    MqttDeviceClient *client_ = nullptr;
    LogManager *logManager_ = nullptr;
    MqttSettingsRepository repository_;
    MqttConnectionOptions options_;
    bool moving_ = false;
    bool safetyStopPending_ = false;
};
