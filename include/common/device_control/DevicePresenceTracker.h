#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>

#include "device_control/DeviceControlTypes.h"

/** Owns bounded heartbeat liveness state on its Qt owner thread. */
class DevicePresenceTracker final : public QObject
{
    Q_OBJECT
public:
    static constexpr int kOfflineTimeoutMs = 30'000;
    static constexpr int kMaximumTrackedDevices = 64;

    using Clock = std::function<qint64()>;

    explicit DevicePresenceTracker(QObject *parent = nullptr, Clock clock = {});

    void registerDevice(const QString &deviceId);
    void unregisterDevice(const QString &deviceId);
    void setAvailable(bool available);
    void processHeartbeat(const DeviceHeartbeat &heartbeat);
    void clearSession();
    [[nodiscard]] DevicePresenceState state(const QString &deviceId) const;
    [[nodiscard]] int trackedDeviceCount() const noexcept;

public slots:
    void refresh();

signals:
    void presenceChanged(const QString &deviceId, DevicePresenceState state);

private:
    struct Entry
    {
        qint64 registeredAtMs = -1;
        qint64 lastSeenAtMs = -1;
        qint64 lastTouchedAtMs = -1;
        bool registered = false;
        DevicePresenceState lastEmitted = DevicePresenceState::Unavailable;
    };

    [[nodiscard]] qint64 nowMs() const;
    [[nodiscard]] DevicePresenceState stateFor(const Entry &entry,
                                               qint64 now) const;
    void emitIfChanged(const QString &deviceId, Entry &entry, qint64 now);
    void trimUnknownDevices();

    Clock clock_;
    QTimer timer_;
    QHash<QString, Entry> entries_;
    bool available_ = false;
};
