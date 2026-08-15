#pragma once

#include <QObject>

#include "device_control/DeviceControlTypes.h"
#include "device_control/MqttSettingsRepository.h"
#include "media/PlaybackTypes.h"

class DeviceControlPanel;
class LogManager;
class MainWindow;
class MqttDeviceClient;
class DevicePresenceTracker;

class DeviceControlController final : public QObject
{
    Q_OBJECT
public:
    DeviceControlController(MainWindow *mainWindow,
                            DeviceControlPanel *panel,
                            MqttDeviceClient *client,
                            DevicePresenceTracker *presenceTracker,
                            LogManager *logManager,
                            MqttSettingsRepository repository = MqttSettingsRepository(),
                            QObject *parent = nullptr);
    ~DeviceControlController() override;

    void start();
    void stop();
    void requestSafetyStop();

public slots:
    void setControlTarget(StreamId streamId, const QString &deviceId,
                          const QString &streamUrl);
    void setDevicePresence(const QString &deviceId, DevicePresenceState state);

private:
    void send(DeviceCommand command);
    void stopMovement();
    void showSettings();
    void handleObservedMessage(const MqttObservedMessage &message);

    MainWindow *mainWindow_ = nullptr;
    DeviceControlPanel *panel_ = nullptr;
    MqttDeviceClient *client_ = nullptr;
    DevicePresenceTracker *presenceTracker_ = nullptr;
    LogManager *logManager_ = nullptr;
    MqttSettingsRepository repository_;
    MqttConnectionOptions options_;
    StreamId targetStreamId_ = kInvalidStreamId;
    QString targetDeviceId_;
    QString targetStreamUrl_;
    DevicePresenceState targetPresence_ = DevicePresenceState::Unavailable;
    bool moving_ = false;
    bool safetyStopPending_ = false;
};
