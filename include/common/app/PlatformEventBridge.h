#pragma once

#include <QHash>
#include <QObject>
#include <QSet>

#include "app/StreamConnectionController.h"
#include "device_control/DeviceControlTypes.h"
#include "event_center/EventCenterTypes.h"
#include "server/MediaServerTypes.h"

class EventCenterService;
struct ControlAttemptSnapshot;

/** Converts existing read-only application observations into event inputs. */
class PlatformEventBridge final : public QObject
{
    Q_OBJECT

public:
    explicit PlatformEventBridge(EventCenterService *service,
                                 QObject *parent = nullptr);

    void setMediaServerEndpoint(const MediaServerEndpoint &endpoint);
    void observeMqttState(MqttConnectionState state);
    void observeMqttSignalingState(MqttConnectionState state);
    void observeDeviceBound(const QString &deviceId);
    void observeDeviceUnbound(const QString &deviceId);
    void observePresence(const QString &deviceId, DevicePresenceState state);
    void observeStream(const StreamEventObservation &observation);
    void observeStreamRemoved(const StreamEventObservation &observation);
    void observeMediaHealth(const MediaServerHealth &health);
    void observeControlAttempt(const ControlAttemptSnapshot &attempt);
    void beginShutdown();
    void stopAccepting();

    [[nodiscard]] QList<EventResourceDescriptor> resources() const;
    [[nodiscard]] static QString localActorName();

signals:
    void resourcesChanged(const QList<EventResourceDescriptor> &resources);

private:
    struct StreamState
    {
        StreamEventObservation observation;
        bool seenPlaying = false;
        bool faultOpen = false;
    };

    [[nodiscard]] QString controlResourceId(
        const ControlAttemptSnapshot &attempt) const;
    [[nodiscard]] static EventControlAttemptSummary controlSummary(
        const ControlAttemptSnapshot &attempt);
    void submitFault(const EventObservation &observation);
    void submitRecovery(const EventObservation &observation);
    void publishResources();

    EventCenterService *service_ = nullptr;
    QSet<QString> registeredDevices_;
    QSet<QString> devicesSeenOnline_;
    QSet<QString> deviceFaults_;
    QHash<StreamId, StreamState> streams_;
    QString mediaServerResourceId_;
    bool mqttSeenConnected_ = false;
    bool mqttFaultOpen_ = false;
    bool signalingRegistered_ = false;
    bool signalingSeenConnected_ = false;
    bool signalingFaultOpen_ = false;
    bool mediaServerSeenHealthy_ = false;
    bool mediaServerFaultOpen_ = false;
    bool movementPotential_ = false;
    bool shutdownMode_ = false;
    bool accepting_ = true;
};
