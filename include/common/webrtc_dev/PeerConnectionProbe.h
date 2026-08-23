#pragma once

#include <QString>
#include <QStringList>

#include <chrono>
#include <memory>

namespace rtmp_monitor::webrtc_dev {

enum class ProbeError {
    None,
    InvalidState,
    LibraryFailure,
    GatheringTimeout,
    ConnectionTimeout,
    ConnectionFailed,
};

struct ProbeDescriptionResult
{
    ProbeError error = ProbeError::None;
    QString sdp;
    QStringList candidateTypes;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == ProbeError::None && !sdp.isEmpty();
    }
};

struct ProbeConnectionResult
{
    ProbeError error = ProbeError::None;
    QStringList candidateTypes;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == ProbeError::None;
    }
};

class PeerConnectionProbe final
{
public:
    PeerConnectionProbe();
    ~PeerConnectionProbe();

    PeerConnectionProbe(const PeerConnectionProbe &) = delete;
    PeerConnectionProbe &operator=(const PeerConnectionProbe &) = delete;

    [[nodiscard]] ProbeDescriptionResult createOffer(
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );
    [[nodiscard]] ProbeDescriptionResult createAnswer(
        const QString &offerSdp,
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );
    [[nodiscard]] ProbeConnectionResult applyAnswerAndWait(
        const QString &answerSdp,
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );
    [[nodiscard]] ProbeConnectionResult waitConnected(
        std::chrono::milliseconds timeout = std::chrono::seconds(30)
    );
    void close() noexcept;

    [[nodiscard]] static QString errorName(ProbeError error);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtmp_monitor::webrtc_dev
