#include "webrtc_runtime/WebRtcReceiveSession.h"

#include "webrtc_dev/SessionPackage.h"

#include <QDeadlineTimer>
#include <QThread>

#include <utility>

namespace rtmp_monitor::webrtc_runtime {
namespace {

using namespace rtmp_monitor::webrtc_dev;
using namespace rtmp_monitor::webrtc_transport;

struct LocatedPackage
{
    QString path;
    SessionPackage package;
};

SessionError findSingleValid(
    SessionPackageStore &store,
    SessionRole role,
    const QString &sessionId,
    LocatedPackage *located
)
{
    QList<LocatedPackage> valid;
    SessionError firstError = SessionError::None;
    for (const QString &path : store.managedFiles(role)) {
        const SessionFileResult read = store.read(path);
        if (!read.ok()) {
            if (firstError == SessionError::None) firstError = read.error;
            continue;
        }
        const SessionResult decoded = SessionPackageCodec::decodeAndValidate(
            read.bytes,
            QDateTime::currentDateTimeUtc(),
            SessionExpectation {role, sessionId, false}
        );
        if (decoded.ok()) {
            valid.push_back({path, *decoded.package});
        } else if (firstError == SessionError::None) {
            firstError = decoded.error;
        }
    }
    if (valid.isEmpty()) {
        return firstError == SessionError::None
                   ? SessionError::NotFound
                   : firstError;
    }
    if (valid.size() != 1) return SessionError::AmbiguousInput;
    *located = std::move(valid.front());
    return SessionError::None;
}

SessionError waitForPackage(
    SessionPackageStore &store,
    SessionRole role,
    const QString &sessionId,
    std::chrono::milliseconds timeout,
    const std::atomic_bool &stopRequested,
    LocatedPackage *located
)
{
    QDeadlineTimer deadline(static_cast<qint64>(timeout.count()));
    SessionError error = SessionError::NotFound;
    while (!deadline.hasExpired() &&
           !stopRequested.load(std::memory_order_acquire)) {
        error = findSingleValid(store, role, sessionId, located);
        if (error != SessionError::NotFound) return error;
        QThread::msleep(50);
    }
    return error;
}

} // namespace

WebRtcReceiveSession::WebRtcReceiveSession(
    WebRtcReceiveSessionOptions options,
    H264ReceiveSink receiveSink,
    ReceiveSessionEventSink eventSink
)
    : options_(std::move(options)),
      receiveSink_(std::move(receiveSink)),
      eventSink_(std::move(eventSink))
{
}

WebRtcReceiveSession::~WebRtcReceiveSession()
{
    requestStop();
    join();
}

bool WebRtcReceiveSession::start()
{
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return false;
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { run(); });
    return true;
}

void WebRtcReceiveSession::requestStop() noexcept
{
    stopRequested_.store(true, std::memory_order_release);
    const std::lock_guard lock(resourcesMutex_);
    if (endpoint_) endpoint_->beginClose();
}

void WebRtcReceiveSession::join() noexcept
{
    if (worker_.joinable()) worker_.join();
}

bool WebRtcReceiveSession::isRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

EndpointSnapshot WebRtcReceiveSession::snapshot() const noexcept
{
    const std::lock_guard lock(resourcesMutex_);
    return endpoint_ ? endpoint_->snapshot() : terminalSnapshot_;
}

std::optional<EndpointConnectionResult>
WebRtcReceiveSession::connectionResult() const
{
    const std::lock_guard lock(resourcesMutex_);
    return connectionResult_;
}

void WebRtcReceiveSession::run() noexcept
{
    try {
        runSession();
    } catch (...) {
        emitFailure(QStringLiteral("library_failure"));
    }
    running_.store(false, std::memory_order_release);
}

void WebRtcReceiveSession::runSession()
{
    SessionPackageStore store(options_.exchangeRoot);
    const SessionError prepareError = store.prepare();
    if (prepareError != SessionError::None) {
        emitFailure(SessionPackageCodec::errorName(prepareError));
        return;
    }
    (void)store.cleanupExpired(QDateTime::currentDateTimeUtc());

    WebRtcSessionConfig configuration;
    configuration.signalingRole = options_.signalingRole;
    configuration.videoDirection = VideoDirection::ReceiveOnly;
    configuration.ice = options_.ice;
    {
        const std::lock_guard lock(resourcesMutex_);
        endpoint_ = std::make_unique<WebRtcEndpointSession>(
            std::move(configuration)
        );
    }
    WebRtcEndpointSession *endpoint = nullptr;
    {
        const std::lock_guard lock(resourcesMutex_);
        endpoint = endpoint_.get();
    }
    if (!endpoint || endpoint->setReceiveSink(receiveSink_) !=
                         EndpointError::None) {
        emitFailure(QStringLiteral("missing_receive_sink"));
        return;
    }
    emitEvent({ReceiveSessionEventKind::Started});

    QString localPackagePath;
    QString remotePackagePath;
    EndpointConnectionResult connected;
    bool signalingFailed = false;

    if (options_.signalingRole == SignalingRole::Offerer) {
        const EndpointDescriptionResult offer =
            endpoint->createOffer(options_.signalingTimeout);
        if (!offer.ok()) {
            EndpointConnectionResult failure;
            failure.error = offer.error;
            failure.candidateTypes = offer.candidateTypes;
            failure.iceState = offer.iceState;
            emitFailure(
                QString::fromLatin1(WebRtcEndpointSession::errorName(offer.error)),
                failure
            );
            signalingFailed = true;
        } else {
            const SessionPackage package = SessionPackageCodec::create(
                SessionRole::Offer, QString::fromStdString(offer.sdp)
            );
            const SessionFileResult written = store.write(package);
            if (!written.ok()) {
                emitFailure(SessionPackageCodec::errorName(written.error));
                signalingFailed = true;
            } else {
                localPackagePath = written.filePath;
                emitEvent({ReceiveSessionEventKind::DescriptionExported});
                LocatedPackage answer;
                const SessionError answerError = waitForPackage(
                    store, SessionRole::Answer, package.sessionId,
                    options_.signalingTimeout, stopRequested_, &answer
                );
                if (answerError != SessionError::None) {
                    emitFailure(
                        stopRequested_.load(std::memory_order_acquire)
                            ? QStringLiteral("cancelled")
                            : SessionPackageCodec::errorName(answerError)
                    );
                    signalingFailed = true;
                } else {
                    remotePackagePath = answer.path;
                    connected = endpoint->acceptAnswerAndWait(
                        answer.package.sdp.toStdString(),
                        options_.signalingTimeout
                    );
                }
            }
        }
    } else {
        LocatedPackage offer;
        const SessionError offerError = waitForPackage(
            store, SessionRole::Offer, {}, options_.signalingTimeout,
            stopRequested_, &offer
        );
        if (offerError != SessionError::None) {
            emitFailure(
                stopRequested_.load(std::memory_order_acquire)
                    ? QStringLiteral("cancelled")
                    : SessionPackageCodec::errorName(offerError)
            );
            signalingFailed = true;
        } else {
            remotePackagePath = offer.path;
            const EndpointDescriptionResult answer =
                endpoint->acceptOfferAndCreateAnswer(
                    offer.package.sdp.toStdString(),
                    options_.signalingTimeout
                );
            if (!answer.ok()) {
                EndpointConnectionResult failure;
                failure.error = answer.error;
                failure.candidateTypes = answer.candidateTypes;
                failure.iceState = answer.iceState;
                emitFailure(
                    QString::fromLatin1(
                        WebRtcEndpointSession::errorName(answer.error)
                    ),
                    failure
                );
                signalingFailed = true;
            } else {
                (void)store.remove(remotePackagePath);
                remotePackagePath.clear();
                const SessionPackage package = SessionPackageCodec::create(
                    SessionRole::Answer,
                    QString::fromStdString(answer.sdp),
                    QDateTime::currentDateTimeUtc(),
                    offer.package.sessionId
                );
                const SessionFileResult written = store.write(package);
                if (!written.ok()) {
                    emitFailure(SessionPackageCodec::errorName(written.error));
                    signalingFailed = true;
                } else {
                    localPackagePath = written.filePath;
                    emitEvent({ReceiveSessionEventKind::DescriptionExported});
                    connected = endpoint->waitConnected(
                        options_.signalingTimeout
                    );
                }
            }
        }
    }

    if (!remotePackagePath.isEmpty()) (void)store.remove(remotePackagePath);
    if (!localPackagePath.isEmpty()) (void)store.remove(localPackagePath);
    if (signalingFailed) {
        endpoint->close();
        rememberTerminalSnapshot();
        const std::lock_guard lock(resourcesMutex_);
        endpoint_.reset();
        return;
    }
    {
        const std::lock_guard lock(resourcesMutex_);
        connectionResult_ = connected;
    }
    if (!connected.ok()) {
        emitFailure(
            QString::fromLatin1(
                WebRtcEndpointSession::errorName(connected.error)
            ),
            connected
        );
        endpoint->close();
        rememberTerminalSnapshot();
        const std::lock_guard lock(resourcesMutex_);
        endpoint_.reset();
        return;
    }
    ReceiveSessionEvent connectedEvent;
    connectedEvent.kind = ReceiveSessionEventKind::Connected;
    connectedEvent.connection = connected;
    connectedEvent.snapshot = endpoint->snapshot();
    emitEvent(std::move(connectedEvent));

    bool connectionLost = false;
    while (!stopRequested_.load(std::memory_order_acquire)) {
        const EndpointSnapshot current = endpoint->snapshot();
        if (current.state == EndpointState::Failed) {
            connectionLost = true;
            ReceiveSessionEvent event;
            event.kind = ReceiveSessionEventKind::ConnectionLost;
            event.reason = QStringLiteral("connection_lost");
            event.connection = connected;
            event.snapshot = current;
            emitEvent(std::move(event));
            break;
        }
        if (current.state == EndpointState::Closed) break;
        QThread::msleep(25);
    }

    endpoint->beginClose();
    endpoint->close();
    rememberTerminalSnapshot();
    {
        const std::lock_guard lock(resourcesMutex_);
        endpoint_.reset();
    }
    if (stopRequested_.load(std::memory_order_acquire) && !connectionLost) {
        emitEvent({ReceiveSessionEventKind::Cancelled,
                   QStringLiteral("cancelled")});
    }
}

void WebRtcReceiveSession::emitEvent(ReceiveSessionEvent event) const
{
    if (eventSink_) eventSink_(std::move(event));
}

void WebRtcReceiveSession::emitFailure(
    const QString &reason,
    const EndpointConnectionResult &result
) const
{
    ReceiveSessionEvent event;
    event.kind = reason == QStringLiteral("cancelled")
                     ? ReceiveSessionEventKind::Cancelled
                     : ReceiveSessionEventKind::Failed;
    event.reason = reason;
    event.connection = result;
    emitEvent(std::move(event));
}

void WebRtcReceiveSession::rememberTerminalSnapshot() noexcept
{
    const std::lock_guard lock(resourcesMutex_);
    if (endpoint_) terminalSnapshot_ = endpoint_->snapshot();
}

} // namespace rtmp_monitor::webrtc_runtime
