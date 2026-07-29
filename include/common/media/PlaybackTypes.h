#pragma once

#include <QImage>
#include <QMetaType>
#include <QSize>
#include <QString>

#include <cstdint>

using StreamId = std::uint64_t;

constexpr StreamId kInvalidStreamId = 0;

/**
 * @brief 描述一路设备从未连接到播放、重连和最终错误的统一状态。
 */
enum class DeviceStatus {
    Disconnected,
    Connecting,
    Playing,
    Reconnecting,
    Error,
};

enum class PlaybackErrorCode {
    AlreadyRunning,
    RuntimeInitializationFailed,
    InvalidConfiguration,
    ConnectionTimeout,
    HostUnavailable,
    AuthenticationFailed,
    MediaUnavailable,
    UnsupportedMedia,
    ResourceFailure,
    DecodeFailure,
    RetryLimitReached,
    Unknown,
};

struct PlaybackError
{
    PlaybackErrorCode code = PlaybackErrorCode::Unknown;
    int nativeCode = 0;
    QString technicalMessage;
    bool recoverable = true;
};

struct StreamConnection
{
    StreamId id = kInvalidStreamId;
    QString displayName;
    QString url;
};

struct PresentationTarget
{
    QSize viewportSize {640, 360};
    bool fullscreen = false;
};

struct PresentableVideoFrame
{
    QImage image;
    std::uint64_t sequence = 0;
    qint64 receivedMonotonicMs = 0;
    qint64 sourceTimestampMs = -1;
};

struct StreamMetrics
{
    StreamId streamId = kInvalidStreamId;
    QString displayName;
    QString state;
    std::uint64_t packetsReceived = 0;
    std::uint64_t packetBytesReceived = 0;
    std::uint64_t packetsDropped = 0;
    std::uint64_t decodedFrames = 0;
    std::uint64_t convertedFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::uint64_t reconnectCount = 0;
    int queuePackets = 0;
    qint64 queueBytes = 0;
    double decodeFps = 0.0;
    double displayFps = 0.0;
    qint64 lastFrameAgeMs = -1;
    qint64 internalLatencyP95Ms = -1;
    qint64 sourceLatencyP50Ms = -1;
    qint64 sourceLatencyP95Ms = -1;
    qint64 sourceLatencyMaxMs = -1;
    std::uint64_t sourceLatencySamples = 0;
};

struct PlaybackPerformanceOptions
{
    int decodeWorkerCount = 0;
    int gridFps = 15;
    int fullscreenFps = 30;
    QSize gridMaximumSize {640, 360};
    QSize fullscreenMaximumSize {1280, 720};
    int maximumQueuedPackets = 45;
    qint64 maximumQueuedBytes = 4 * 1024 * 1024;
    bool latencyMarkerEnabled = false;
    int reconnectDelayMs = 3'000;
    int maximumConsecutiveFailures = 0;
};

Q_DECLARE_METATYPE(DeviceStatus)
Q_DECLARE_METATYPE(PlaybackError)
Q_DECLARE_METATYPE(StreamConnection)
Q_DECLARE_METATYPE(PresentableVideoFrame)
Q_DECLARE_METATYPE(StreamMetrics)
