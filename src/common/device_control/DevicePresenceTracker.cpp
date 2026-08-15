#include "device_control/DevicePresenceTracker.h"

#include <QElapsedTimer>

#include <algorithm>

DevicePresenceTracker::DevicePresenceTracker(QObject *parent, Clock clock)
    : QObject(parent), clock_(std::move(clock))
{
    if (!clock_) {
        clock_ = [] {
            QElapsedTimer monotonicClock;
            monotonicClock.start();
            return monotonicClock.msecsSinceReference();
        };
    }
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &DevicePresenceTracker::refresh);
    timer_.start();
}

void DevicePresenceTracker::registerDevice(const QString &deviceId)
{
    const QString normalized = deviceId.trimmed();
    if (normalized.isEmpty()) return;
    const qint64 now = nowMs();
    Entry &entry = entries_[normalized];
    if (!entry.registered) {
        entry.registered = true;
        entry.registeredAtMs = now;
        entry.lastTouchedAtMs = now;
    }
    emitIfChanged(normalized, entry, now);
    trimUnknownDevices();
}

void DevicePresenceTracker::unregisterDevice(const QString &deviceId)
{
    auto it = entries_.find(deviceId.trimmed());
    if (it == entries_.end()) return;
    it->registered = false;
    it->lastTouchedAtMs = nowMs();
    trimUnknownDevices();
}

void DevicePresenceTracker::setAvailable(bool available)
{
    if (available_ == available) return;
    available_ = available;
    refresh();
}

void DevicePresenceTracker::processHeartbeat(const DeviceHeartbeat &heartbeat)
{
    if (heartbeat.clientId.isEmpty()) return;
    const qint64 now = nowMs();
    Entry &entry = entries_[heartbeat.clientId];
    if (entry.registeredAtMs < 0) entry.registeredAtMs = now;
    entry.lastSeenAtMs = heartbeat.receivedAtMonotonicMs >= 0
        ? heartbeat.receivedAtMonotonicMs : now;
    entry.lastTouchedAtMs = now;
    emitIfChanged(heartbeat.clientId, entry, now);
    trimUnknownDevices();
}

void DevicePresenceTracker::clearSession()
{
    const qint64 now = nowMs();
    available_ = false;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!it->registered) {
            it = entries_.erase(it);
            continue;
        }
        it->registeredAtMs = now;
        it->lastSeenAtMs = -1;
        it->lastTouchedAtMs = now;
        it->lastEmitted = DevicePresenceState::Unavailable;
        emit presenceChanged(it.key(), DevicePresenceState::Unavailable);
        ++it;
    }
}

DevicePresenceState DevicePresenceTracker::state(const QString &deviceId) const
{
    const auto it = entries_.constFind(deviceId.trimmed());
    if (it == entries_.cend()) return DevicePresenceState::Unavailable;
    return stateFor(*it, nowMs());
}

int DevicePresenceTracker::trackedDeviceCount() const noexcept
{
    return entries_.size();
}

void DevicePresenceTracker::refresh()
{
    const qint64 now = nowMs();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        emitIfChanged(it.key(), it.value(), now);
    }
    trimUnknownDevices();
}

qint64 DevicePresenceTracker::nowMs() const { return clock_(); }

DevicePresenceState DevicePresenceTracker::stateFor(const Entry &entry,
                                                    qint64 now) const
{
    if (!available_) return DevicePresenceState::Unavailable;
    if (entry.lastSeenAtMs >= 0 &&
        now - entry.lastSeenAtMs < kOfflineTimeoutMs) {
        return DevicePresenceState::Online;
    }
    if (entry.lastSeenAtMs < 0 &&
        now - entry.registeredAtMs < kOfflineTimeoutMs) {
        return DevicePresenceState::Waiting;
    }
    return DevicePresenceState::Offline;
}

void DevicePresenceTracker::emitIfChanged(const QString &deviceId, Entry &entry,
                                          qint64 now)
{
    const DevicePresenceState next = stateFor(entry, now);
    if (entry.lastEmitted == next) return;
    entry.lastEmitted = next;
    emit presenceChanged(deviceId, next);
}

void DevicePresenceTracker::trimUnknownDevices()
{
    const qint64 now = nowMs();
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!it->registered && it->lastTouchedAtMs >= 0 &&
            now - it->lastTouchedAtMs >= kOfflineTimeoutMs) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    while (entries_.size() > kMaximumTrackedDevices) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->registered) continue;
            if (oldest == entries_.end() ||
                it->lastTouchedAtMs < oldest->lastTouchedAtMs) {
                oldest = it;
            }
        }
        if (oldest == entries_.end()) return;
        entries_.erase(oldest);
    }
}
