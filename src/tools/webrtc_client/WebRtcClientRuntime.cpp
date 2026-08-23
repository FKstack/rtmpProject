#include "webrtc_client/WebRtcClientRuntime.h"

#include "webrtc_dev/SessionPackage.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QThread>

#include <chrono>
#include <optional>
#include <utility>

namespace rtmp_monitor::webrtc_client {
namespace {

using namespace rtmp_monitor::publisher;
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

QJsonArray candidateArray(const std::vector<std::string> &types)
{
    QJsonArray result;
    for (const std::string &type : types) {
        result.push_back(QString::fromStdString(type));
    }
    return result;
}

QJsonObject candidatePairObject(const EndpointCandidatePair &pair)
{
    return {
        {QStringLiteral("localType"),
         QString::fromStdString(pair.localType)},
        {QStringLiteral("remoteType"),
         QString::fromStdString(pair.remoteType)},
        {QStringLiteral("localTransport"),
         QString::fromStdString(pair.localTransport)},
        {QStringLiteral("remoteTransport"),
         QString::fromStdString(pair.remoteTransport)}
    };
}

} // namespace

WebRtcClientRuntime::WebRtcClientRuntime(
    WebRtcClientOptions options,
    ClientEventSink eventSink,
    H264ReceiveSink receiveSink,
    std::shared_ptr<WebRtcViewerEvidence> viewerEvidence
)
    : options_(std::move(options)),
      eventSink_(std::move(eventSink)),
      receiveSink_(std::move(receiveSink)),
      viewerEvidence_(std::move(viewerEvidence))
{
}

WebRtcClientRuntime::~WebRtcClientRuntime()
{
    requestStop();
    join();
}

bool WebRtcClientRuntime::start()
{
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return false;
    worker_ = std::thread([this] { run(); });
    return true;
}

void WebRtcClientRuntime::requestStop() noexcept
{
    stopRequested_.store(true, std::memory_order_release);
    const std::lock_guard lock(resourcesMutex_);
    if (endpoint_) endpoint_->beginClose();
    if (source_) source_->stop();
}

void WebRtcClientRuntime::join() noexcept
{
    if (worker_.joinable()) worker_.join();
}

int WebRtcClientRuntime::exitCode() const noexcept
{
    return exitCode_.load(std::memory_order_acquire);
}

void WebRtcClientRuntime::run() noexcept
{
    int result = 4;
    try {
        result = runSession();
    } catch (...) {
        emitFailure(QStringLiteral("library_failure"));
    }
    exitCode_.store(result, std::memory_order_release);
}

int WebRtcClientRuntime::runSession()
{
    runtimePaths_ = WebRtcClientRuntimePaths::resolve(
        QCoreApplication::applicationDirPath(),
        QDir::currentPath()
    );
    if (!runtimePaths_.ok()) {
        emitFailure(QStringLiteral("unsafe_path"));
        return 3;
    }
    emitEvent(
        QStringLiteral("runtime_ready"),
        QJsonObject {
            {QStringLiteral("layout"),
             WebRtcClientRuntimePaths::layoutName(runtimePaths_.layout)}
        }
    );
    if (options_.mediaRole == ClientMediaRole::Publisher) {
        if (!QFileInfo(runtimePaths_.samplePath).isFile()) {
            emitFailure(QStringLiteral("file_not_found"));
            return 4;
        }
    }
    SessionPackageStore store(runtimePaths_.exchangeRoot);
    const SessionError prepareError = store.prepare();
    if (prepareError != SessionError::None) {
        emitFailure(SessionPackageCodec::errorName(prepareError));
        return 3;
    }

    WebRtcSessionConfig configuration;
    configuration.signalingRole = options_.signalingRole;
    configuration.videoDirection =
        options_.mediaRole == ClientMediaRole::Publisher
            ? VideoDirection::SendOnly
            : VideoDirection::ReceiveOnly;
    {
        const std::lock_guard lock(resourcesMutex_);
        endpoint_ = std::make_unique<WebRtcEndpointSession>(configuration);
    }
    WebRtcEndpointSession *endpoint = nullptr;
    {
        const std::lock_guard lock(resourcesMutex_);
        endpoint = endpoint_.get();
    }
    if (endpoint == nullptr) {
        emitFailure(QStringLiteral("resource_failure"));
        return 4;
    }
    if (options_.mediaRole == ClientMediaRole::Viewer) {
        const EndpointError sinkError =
            endpoint->setReceiveSink(receiveSink_);
        if (sinkError != EndpointError::None) {
            emitFailure(
                QString::fromLatin1(
                    WebRtcEndpointSession::errorName(sinkError)
                )
            );
            endpoint->close();
            return 4;
        }
    }

    QString localPackagePath;
    QString remotePackagePath;
    EndpointConnectionResult connected;
    int signalingResult = 0;
    if (options_.signalingRole == SignalingRole::Offerer) {
        const EndpointDescriptionResult offer =
            endpoint->createOffer(options_.timeout);
        if (!offer.ok()) {
            emitFailure(QString::fromLatin1(
                WebRtcEndpointSession::errorName(offer.error)
            ));
            signalingResult = 4;
        } else {
            const SessionPackage package = SessionPackageCodec::create(
                SessionRole::Offer, QString::fromStdString(offer.sdp)
            );
            const SessionFileResult written = store.write(package);
            if (!written.ok()) {
                emitFailure(SessionPackageCodec::errorName(written.error));
                signalingResult = 3;
            } else {
                localPackagePath = written.filePath;
                emitEvent(QStringLiteral("description_exported"));
                LocatedPackage answer;
                const SessionError answerError = waitForPackage(
                    store,
                    SessionRole::Answer,
                    package.sessionId,
                    options_.timeout,
                    stopRequested_,
                    &answer
                );
                if (answerError != SessionError::None) {
                    emitFailure(
                        stopRequested_.load(std::memory_order_acquire)
                            ? QStringLiteral("cancelled")
                            : SessionPackageCodec::errorName(answerError)
                    );
                    signalingResult = 3;
                } else {
                    remotePackagePath = answer.path;
                    connected = endpoint->acceptAnswerAndWait(
                        answer.package.sdp.toStdString(), options_.timeout
                    );
                }
            }
        }
    } else {
        LocatedPackage offer;
        const SessionError offerError = waitForPackage(
            store,
            SessionRole::Offer,
            {},
            options_.timeout,
            stopRequested_,
            &offer
        );
        if (offerError != SessionError::None) {
            emitFailure(
                stopRequested_.load(std::memory_order_acquire)
                    ? QStringLiteral("cancelled")
                    : SessionPackageCodec::errorName(offerError)
            );
            signalingResult = 3;
        } else {
            remotePackagePath = offer.path;
            const EndpointDescriptionResult answer =
                endpoint->acceptOfferAndCreateAnswer(
                    offer.package.sdp.toStdString(), options_.timeout
                );
            if (!answer.ok()) {
                emitFailure(QString::fromLatin1(
                    WebRtcEndpointSession::errorName(answer.error)
                ));
                signalingResult = 4;
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
                    signalingResult = 3;
                } else {
                    localPackagePath = written.filePath;
                    emitEvent(QStringLiteral("description_exported"));
                    connected = endpoint->waitConnected(options_.timeout);
                }
            }
        }
    }

    if (!remotePackagePath.isEmpty()) (void)store.remove(remotePackagePath);
    if (!localPackagePath.isEmpty()) (void)store.remove(localPackagePath);
    if (signalingResult != 0) {
        endpoint->close();
        const std::lock_guard lock(resourcesMutex_);
        endpoint_.reset();
        return signalingResult;
    }
    if (!connected.ok()) {
        emitFailure(QString::fromLatin1(
            WebRtcEndpointSession::errorName(connected.error)
        ));
        endpoint->close();
        const std::lock_guard lock(resourcesMutex_);
        endpoint_.reset();
        return 4;
    }

    QJsonObject connectedDetails;
    connectedDetails.insert(
        QStringLiteral("candidateTypes"),
        candidateArray(connected.candidateTypes)
    );
    if (connected.selectedPair.has_value()) {
        connectedDetails.insert(
            QStringLiteral("selectedCandidatePair"),
            candidatePairObject(*connected.selectedPair)
        );
    }
    emitEvent(QStringLiteral("connected"), std::move(connectedDetails));

    const int result = options_.mediaRole == ClientMediaRole::Publisher
                           ? runPublisherMedia(*endpoint)
                           : runViewerMedia(*endpoint);
    endpoint->close();
    {
        const std::lock_guard lock(resourcesMutex_);
        endpoint_.reset();
    }
    return result;
}

int WebRtcClientRuntime::runPublisherMedia(WebRtcEndpointSession &endpoint)
{
    const QString samplePath = runtimePaths_.samplePath;
    if (!QFileInfo(samplePath).isFile()) {
        emitFailure(QStringLiteral("file_not_found"));
        return 4;
    }
    auto submitPort = endpoint.createSendPort();
    if (!submitPort.has_value()) {
        emitFailure(QStringLiteral("invalid_state"));
        return 4;
    }

    {
        const std::lock_guard lock(resourcesMutex_);
        source_ = std::make_unique<Mp4H264PublisherSource>();
    }
    Mp4H264PublisherSource *source = nullptr;
    {
        const std::lock_guard lock(resourcesMutex_);
        source = source_.get();
    }
    const PublisherSourceError startError = source->start(
        QFileInfo(samplePath).absoluteFilePath().toStdString(),
        std::move(*submitPort)
    );
    if (startError != PublisherSourceError::None) {
        emitFailure(QString::fromLatin1(
            Mp4H264PublisherSource::errorName(startError)
        ));
        return 4;
    }
    emitEvent(QStringLiteral("publishing"));
    const PublisherSourceError sourceError =
        source->waitForCompletion(options_.timeout);
    const EndpointSnapshot terminalSnapshot = endpoint.snapshot();
    if (terminalSnapshot.state == EndpointState::Failed) {
        emitEvent(QStringLiteral("connection_lost"));
    }
    endpoint.beginClose();
    source->stop();
    endpoint.close();
    const PublisherSourceSnapshot sourceSnapshot = source->snapshot();
    const EndpointSnapshot endpointSnapshot = endpoint.snapshot();
    {
        const std::lock_guard lock(resourcesMutex_);
        source_.reset();
    }
    if (sourceError != PublisherSourceError::None) {
        emitFailure(QString::fromLatin1(
            Mp4H264PublisherSource::errorName(sourceError)
        ));
        return 4;
    }

    QJsonObject details;
    details.insert(
        QStringLiteral("accessUnits"),
        static_cast<double>(sourceSnapshot.emittedAccessUnits)
    );
    details.insert(
        QStringLiteral("keyframes"),
        static_cast<double>(sourceSnapshot.emittedKeyframes)
    );
    details.insert(
        QStringLiteral("sourceDrops"),
        static_cast<double>(sourceSnapshot.droppedAccessUnits)
    );
    details.insert(
        QStringLiteral("sentAccessUnits"),
        static_cast<double>(endpointSnapshot.sentAccessUnits)
    );
    details.insert(
        QStringLiteral("transportDrops"),
        static_cast<double>(endpointSnapshot.droppedAccessUnits)
    );
    details.insert(
        QStringLiteral("sendFailures"),
        static_cast<double>(endpointSnapshot.sendFailures)
    );
    emitEvent(QStringLiteral("completed"), std::move(details));
    return endpointSnapshot.sendFailures == 0 ? 0 : 4;
}

int WebRtcClientRuntime::runViewerMedia(WebRtcEndpointSession &endpoint)
{
    QDeadlineTimer deadline(static_cast<qint64>(options_.timeout.count()));
    bool mediaEventEmitted = false;
    EndpointSnapshot snapshot;
    while (!deadline.hasExpired() &&
           !stopRequested_.load(std::memory_order_acquire)) {
        snapshot = endpoint.snapshot();
        if (!mediaEventEmitted && snapshot.receivedAccessUnits > 0) {
            QJsonObject details;
            details.insert(
                QStringLiteral("receivedRtpPackets"),
                static_cast<double>(snapshot.receivedRtpPackets)
            );
            details.insert(
                QStringLiteral("receivedAccessUnits"),
                static_cast<double>(snapshot.receivedAccessUnits)
            );
            details.insert(
                QStringLiteral("submittedAccessUnits"),
                static_cast<double>(snapshot.submittedAccessUnits)
            );
            emitEvent(QStringLiteral("media_received"), std::move(details));
            mediaEventEmitted = true;
        }
        if (snapshot.state == EndpointState::Failed) {
            emitEvent(QStringLiteral("connection_lost"));
            break;
        }
        if (
            snapshot.state == EndpointState::Closed) {
            break;
        }
        QThread::msleep(25);
    }

    endpoint.beginClose();
    endpoint.close();
    snapshot = endpoint.snapshot();
    const bool decoded = viewerEvidence_ &&
        viewerEvidence_->decoded.load(std::memory_order_acquire);
    const bool presented = viewerEvidence_ &&
        viewerEvidence_->presented.load(std::memory_order_acquire);
    const bool success = mediaEventEmitted &&
        snapshot.submittedAccessUnits > 0 && decoded && presented;

    QJsonObject details;
    details.insert(
        QStringLiteral("receivedRtpPackets"),
        static_cast<double>(snapshot.receivedRtpPackets)
    );
    details.insert(
        QStringLiteral("receivedAccessUnits"),
        static_cast<double>(snapshot.receivedAccessUnits)
    );
    details.insert(
        QStringLiteral("submittedAccessUnits"),
        static_cast<double>(snapshot.submittedAccessUnits)
    );
    details.insert(
        QStringLiteral("receiveDrops"),
        static_cast<double>(snapshot.receiveDrops)
    );
    details.insert(QStringLiteral("decoded"), decoded);
    details.insert(QStringLiteral("presented"), presented);
    if (!success) {
        emitFailure(QStringLiteral("media_timeout"), std::move(details));
        return 4;
    }
    emitEvent(QStringLiteral("completed"), std::move(details));
    return 0;
}

void WebRtcClientRuntime::emitEvent(
    const QString &event,
    QJsonObject details
) const
{
    details.insert(QStringLiteral("mediaRole"), mediaRoleName(options_.mediaRole));
    details.insert(
        QStringLiteral("role"), signalingRoleName(options_.signalingRole)
    );
    if (eventSink_) eventSink_(event, std::move(details));
}

void WebRtcClientRuntime::emitFailure(
    const QString &error,
    QJsonObject details
) const
{
    details.insert(QStringLiteral("error"), error);
    emitEvent(QStringLiteral("failed"), std::move(details));
}

} // namespace rtmp_monitor::webrtc_client
