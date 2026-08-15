#pragma once

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include "media/PlaybackTypes.h"

enum class FfmpegTrackKind;

/** @brief Result of one FFmpeg input attempt; reconnect policy stays in FFmpegPlayer. */
struct FfmpegInputResult
{
    PlaybackErrorCode errorCode = PlaybackErrorCode::Unknown;
    int nativeCode = 0;
    QString message;
    bool receivedPackets = false;
};

/**
 * @brief Owns one blocking RTMP open/probe/read attempt on the network thread.
 *
 * It has no QObject/UI knowledge and reports codec configuration and owned
 * AVPacket pointers through callbacks. The caller owns reconnect policy.
 */
class FfmpegInputSession final
{
public:
    using ConfigurationCallback =
        std::function<void(const std::shared_ptr<void> &, std::uint64_t)>;
    using PacketCallback =
        std::function<void(FfmpegTrackKind, void *, qint64, std::uint64_t)>;

    FfmpegInputSession(
        const std::atomic_bool &stopRequested,
        const std::atomic_bool &restartRequested,
        ConfigurationCallback configurationCallback,
        PacketCallback packetCallback
    );

    [[nodiscard]] static bool networkRuntimeAvailable() noexcept;
    [[nodiscard]] FfmpegInputResult run(
        const QString &rtmpUrl,
        std::uint64_t sessionId
    );

private:
    static int interruptCallback(void *opaque) noexcept;
    [[nodiscard]] bool interrupted() const noexcept;

    const std::atomic_bool &stopRequested_;
    const std::atomic_bool &restartRequested_;
    ConfigurationCallback configurationCallback_;
    PacketCallback packetCallback_;
};
