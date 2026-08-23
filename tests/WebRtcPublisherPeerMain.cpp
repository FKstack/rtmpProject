#include "webrtc_dev/SessionPackage.h"
#include "webrtc_contracts/WebRtcSessionContracts.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>

#include <rtc/rtc.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

using namespace rtmp_monitor::webrtc_dev;

namespace {

constexpr std::uint8_t kPayloadType = 102;

struct LocatedPackage
{
    QString path;
    SessionPackage package;
};

struct ReceiverState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool gatheringComplete = false;
    bool connected = false;
    bool failed = false;
    bool closing = false;
    std::uint64_t accessUnits = 0;
    bool recoverableKeyframe = false;
};

void emitEvent(const QString &event, const QString &error = {}, int count = -1)
{
    QJsonObject object;
    object.insert(QStringLiteral("event"), event);
    if (!error.isEmpty()) object.insert(QStringLiteral("error"), error);
    if (count >= 0) object.insert(QStringLiteral("count"), count);
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact)
                        << Qt::endl;
}

bool containsRecoverableKeyframe(const rtc::binary &annexB)
{
    bool hasSps = false;
    bool hasPps = false;
    bool hasIdr = false;
    for (std::size_t index = 0; index + 4 < annexB.size(); ++index) {
        std::size_t header = 0;
        if (annexB[index] == std::byte {0} &&
            annexB[index + 1] == std::byte {0} &&
            annexB[index + 2] == std::byte {1}) {
            header = index + 3;
        } else if (index + 4 < annexB.size() &&
                   annexB[index] == std::byte {0} &&
                   annexB[index + 1] == std::byte {0} &&
                   annexB[index + 2] == std::byte {0} &&
                   annexB[index + 3] == std::byte {1}) {
            header = index + 4;
        }
        if (header == 0 || header >= annexB.size()) continue;
        const auto type = std::to_integer<std::uint8_t>(annexB[header]) & 0x1fU;
        hasSps = hasSps || type == 7;
        hasPps = hasPps || type == 8;
        hasIdr = hasIdr || type == 5;
        index = header;
    }
    return hasSps && hasPps && hasIdr;
}

class TestH264Receiver final
{
public:
    bool initialize()
    {
        try {
            rtc::Configuration configuration;
            configuration.disableAutoNegotiation = true;
            configuration.enableIceTcp = false;
            connection_ = std::make_shared<rtc::PeerConnection>(configuration);
            const std::weak_ptr<ReceiverState> weakState(state_);
            connection_->onGatheringStateChange(
                [weakState](rtc::PeerConnection::GatheringState value) {
                    const auto state = weakState.lock();
                    if (!state) return;
                    const std::lock_guard lock(state->mutex);
                    if (state->closing) return;
                    state->gatheringComplete =
                        value == rtc::PeerConnection::GatheringState::Complete;
                    state->changed.notify_all();
                }
            );
            connection_->onStateChange(
                [weakState](rtc::PeerConnection::State value) {
                    const auto state = weakState.lock();
                    if (!state) return;
                    const std::lock_guard lock(state->mutex);
                    if (state->closing) return;
                    state->connected =
                        value == rtc::PeerConnection::State::Connected;
                    state->failed = value == rtc::PeerConnection::State::Failed ||
                                    value == rtc::PeerConnection::State::Closed;
                    state->changed.notify_all();
                }
            );

            rtc::Description::Video video(
                "video", rtc::Description::Direction::RecvOnly
            );
            video.addH264Codec(kPayloadType);
            track_ = connection_->addTrack(video);
            auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(
                rtc::NalUnit::Separator::StartSequence
            );
            depacketizer->addToChain(
                std::make_shared<rtc::RtcpReceivingSession>()
            );
            track_->setMediaHandler(std::move(depacketizer));
            track_->onFrame(
                [weakState](rtc::binary data, rtc::FrameInfo) {
                    const auto state = weakState.lock();
                    if (!state) return;
                    const bool recoverable = containsRecoverableKeyframe(data);
                    const std::lock_guard lock(state->mutex);
                    if (state->closing) return;
                    ++state->accessUnits;
                    state->recoverableKeyframe =
                        state->recoverableKeyframe || recoverable;
                    state->changed.notify_all();
                }
            );
            return true;
        } catch (...) {
            close();
            return false;
        }
    }

    std::optional<std::string> createOffer(std::chrono::milliseconds timeout)
    {
        if (!initialize()) return std::nullopt;
        try {
            connection_->setLocalDescription(rtc::Description::Type::Offer);
        } catch (...) {
            return std::nullopt;
        }
        return waitForDescription(timeout);
    }

    std::optional<std::string> acceptOfferAndCreateAnswer(
        const std::string &offer,
        std::chrono::milliseconds timeout
    )
    {
        if (offer.empty() || !initialize()) return std::nullopt;
        try {
            connection_->setRemoteDescription(rtc::Description(offer, "offer"));
            connection_->setLocalDescription(rtc::Description::Type::Answer);
        } catch (...) {
            return std::nullopt;
        }
        return waitForDescription(timeout);
    }

    bool acceptAnswerAndWait(
        const std::string &answer,
        std::chrono::milliseconds timeout
    )
    {
        if (answer.empty() || !connection_) return false;
        try {
            connection_->setRemoteDescription(rtc::Description(answer, "answer"));
        } catch (...) {
            return false;
        }
        return waitConnected(timeout);
    }

    bool waitConnected(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(state_->mutex);
        return state_->changed.wait_for(lock, timeout, [state = state_] {
                   return state->connected || state->failed || state->closing;
               }) && state_->connected;
    }

    std::optional<std::uint64_t> waitForRecoverableKeyframe(
        std::chrono::milliseconds timeout
    )
    {
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_for(lock, timeout, [state = state_] {
                return state->recoverableKeyframe || state->failed ||
                       state->closing;
            }) || !state_->recoverableKeyframe) {
            return std::nullopt;
        }
        return state_->accessUnits;
    }

    std::uint64_t accessUnitCount() const noexcept
    {
        const std::lock_guard lock(state_->mutex);
        return state_->accessUnits;
    }

    void close() noexcept
    {
        {
            const std::lock_guard lock(state_->mutex);
            if (state_->closing) return;
            state_->closing = true;
            state_->changed.notify_all();
        }
        try {
            if (track_) track_->resetCallbacks();
            if (connection_) connection_->resetCallbacks();
            if (track_) track_->close();
            if (connection_) connection_->close();
        } catch (...) {
        }
        track_.reset();
        connection_.reset();
    }

    ~TestH264Receiver() { close(); }

private:
    std::optional<std::string> waitForDescription(
        std::chrono::milliseconds timeout
    )
    {
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_for(lock, timeout, [state = state_] {
                return state->gatheringComplete || state->failed ||
                       state->closing;
            }) || !state_->gatheringComplete) {
            return std::nullopt;
        }
        lock.unlock();
        try {
            const auto description = connection_->localDescription();
            if (!description.has_value()) return std::nullopt;
            return description->generateSdp();
        } catch (...) {
            return std::nullopt;
        }
    }

    std::shared_ptr<ReceiverState> state_ = std::make_shared<ReceiverState>();
    std::shared_ptr<rtc::PeerConnection> connection_;
    std::shared_ptr<rtc::Track> track_;
};

SessionError findPackage(
    SessionPackageStore &store,
    SessionRole role,
    const QString &sessionId,
    LocatedPackage *located
)
{
    QList<LocatedPackage> valid;
    for (const QString &path : store.managedFiles(role)) {
        const SessionFileResult read = store.read(path);
        if (!read.ok()) continue;
        const SessionResult decoded = SessionPackageCodec::decodeAndValidate(
            read.bytes,
            QDateTime::currentDateTimeUtc(),
            SessionExpectation {role, sessionId, false}
        );
        if (decoded.ok()) valid.push_back({path, *decoded.package});
    }
    if (valid.isEmpty()) return SessionError::NotFound;
    if (valid.size() != 1) return SessionError::AmbiguousInput;
    *located = valid.front();
    return SessionError::None;
}

SessionError waitForPackage(
    SessionPackageStore &store,
    SessionRole role,
    const QString &sessionId,
    int timeoutMs,
    LocatedPackage *located
)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        const SessionError error = findPackage(store, role, sessionId, located);
        if (error != SessionError::NotFound) return error;
        QThread::msleep(50);
    }
    return SessionError::NotFound;
}

int cleanupManaged(SessionPackageStore &store)
{
    int removed = 0;
    for (const SessionRole role : {SessionRole::Offer, SessionRole::Answer}) {
        for (const QString &path : store.managedFiles(role)) {
            if (store.remove(path) == SessionError::None) ++removed;
        }
    }
    emitEvent(QStringLiteral("cleanup_complete"), {}, removed);
    return 0;
}

int runPeer(SessionPackageStore &store, SignalingRole role, int timeoutMs)
{
    TestH264Receiver receiver;
    const auto timeout = std::chrono::milliseconds(timeoutMs);
    bool connected = false;

    if (role == SignalingRole::Offerer) {
        const auto offer = receiver.createOffer(timeout);
        if (!offer.has_value()) return 4;
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer, QString::fromStdString(*offer)
        );
        const SessionFileResult written = store.write(package);
        if (!written.ok()) return 3;
        emitEvent(QStringLiteral("description_exported"));
        LocatedPackage answer;
        const SessionError answerError = waitForPackage(
            store, SessionRole::Answer, package.sessionId, timeoutMs, &answer
        );
        if (answerError != SessionError::None) {
            (void)store.remove(written.filePath);
            return 3;
        }
        connected = receiver.acceptAnswerAndWait(
            answer.package.sdp.toStdString(), timeout
        );
        (void)store.remove(answer.path);
        (void)store.remove(written.filePath);
    } else {
        LocatedPackage offer;
        const SessionError offerError = waitForPackage(
            store, SessionRole::Offer, {}, timeoutMs, &offer
        );
        if (offerError != SessionError::None) return 3;
        const auto answer = receiver.acceptOfferAndCreateAnswer(
            offer.package.sdp.toStdString(), timeout
        );
        if (!answer.has_value()) return 4;
        if (store.remove(offer.path) != SessionError::None) return 3;
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Answer,
            QString::fromStdString(*answer),
            QDateTime::currentDateTimeUtc(),
            offer.package.sessionId
        );
        const SessionFileResult written = store.write(package);
        if (!written.ok()) return 3;
        emitEvent(QStringLiteral("description_exported"));
        connected = receiver.waitConnected(timeout);
        (void)store.remove(written.filePath);
    }
    if (!connected) {
        emitEvent(QStringLiteral("failed"), QStringLiteral("connection_failed"));
        return 4;
    }
    emitEvent(QStringLiteral("connected"));

    const auto firstRecoverable = receiver.waitForRecoverableKeyframe(timeout);
    if (!firstRecoverable.has_value()) {
        receiver.close();
        emitEvent(QStringLiteral("failed"), QStringLiteral("media_timeout"));
        return 4;
    }
    QThread::msleep(6500);
    const std::uint64_t received = receiver.accessUnitCount();
    receiver.close();
    emitEvent(
        QStringLiteral("recoverable_keyframe_received"),
        {},
        static_cast<int>(received)
    );
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("rtmp_monitor_webrtc_publisher_peer")
    );
    rtc::InitLogger(rtc::LogLevel::None);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("signaling-role"), QStringLiteral("offer or answer"), QStringLiteral("role")});
    parser.addOption({QStringLiteral("timeout-ms"), QStringLiteral("timeout"), QStringLiteral("milliseconds"), QStringLiteral("30000")});
    parser.addOption({QStringLiteral("cleanup"), QStringLiteral("remove managed test packages")});
    if (!parser.parse(QCoreApplication::arguments())) return 2;
    if (parser.isSet(QStringLiteral("help"))) {
        QString helpText = parser.helpText();
        helpText.replace(
            QCoreApplication::arguments().constFirst(),
            QCoreApplication::applicationName()
        );
        QTextStream(stdout) << helpText;
        return 0;
    }

    const QString root = SessionPackageStore::discoverRepositoryRoot(
        QDir::currentPath()
    );
    if (root.isEmpty()) return 3;
    SessionPackageStore store(
        SessionPackageStore::exchangeRootForRepository(root)
    );
    if (store.prepare() != SessionError::None) return 3;

    int result = 0;
    if (parser.isSet(QStringLiteral("cleanup"))) {
        result = cleanupManaged(store);
    } else {
        bool ok = false;
        const int timeoutMs = parser.value(QStringLiteral("timeout-ms")).toInt(&ok);
        const QString role = parser.value(QStringLiteral("signaling-role")).toLower();
        if (!ok || timeoutMs < 1000 || timeoutMs > 600000 ||
            !QStringList {QStringLiteral("offer"), QStringLiteral("answer")}.contains(role)) {
            return 2;
        }
        result = runPeer(
            store,
            role == QStringLiteral("offer")
                ? SignalingRole::Offerer
                : SignalingRole::Answerer,
            timeoutMs
        );
    }

    auto cleanup = rtc::Cleanup();
    if (cleanup.wait_for(std::chrono::seconds(10)) ==
        std::future_status::timeout) return 4;
    cleanup.get();
    return result;
}
