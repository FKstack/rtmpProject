#pragma once

#include <QObject>
#include <QTimer>

#include <memory>

class PlatformEventBridge;

namespace rtmp::p2p {
class DirectOperatorCore;
class MqttSignalingChannel;
struct DirectCoreSnapshot;
}

/** Optional WebRTC-ON composition adapter for the existing desktop process. */
class DirectDesktopRuntime final : public QObject
{
public:
    DirectDesktopRuntime(QString configPath, QString scenario,
                         QString resultPath, PlatformEventBridge *eventBridge,
                         QObject *parent = nullptr);
    ~DirectDesktopRuntime() override;

    bool start(QString *error);
    void stop();

private:
    void handleChanged(const rtmp::p2p::DirectCoreSnapshot &snapshot);
    void finish(bool passed, const QString &error = {});

    QString configPath_;
    QString scenario_;
    QString resultPath_;
    PlatformEventBridge *eventBridge_ = nullptr;
    std::unique_ptr<rtmp::p2p::MqttSignalingChannel> channel_;
    std::unique_ptr<rtmp::p2p::DirectOperatorCore> core_;
    QTimer pollTimer_;
    QTimer timeoutTimer_;
    bool requestStarted_ = false;
    bool duplicateSent_ = false;
    bool duplicateAwaiting_ = false;
    bool reconnectStarted_ = false;
    bool reconnectCompleted_ = false;
    bool finished_ = false;
    int lastChannelState_ = -1;
};
