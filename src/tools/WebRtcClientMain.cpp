#include "publisher/Mp4H264PublisherSource.h"
#include "webrtc_dev/SessionPackage.h"
#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>

#include <rtc/rtc.hpp>

#include <chrono>
#include <future>
#include <optional>
#include <utility>

using namespace rtmp_monitor::publisher;
using namespace rtmp_monitor::webrtc_dev;
using namespace rtmp_monitor::webrtc_transport;

namespace {

struct LocatedPackage
{
    QString path;
    SessionPackage package;
};

void emitEvent(
    const QString &event,
    const QString &role = {},
    const QString &error = {},
    const std::vector<std::string> &candidateTypes = {},
    const PublisherSourceSnapshot *source = nullptr,
    const EndpointSnapshot *endpoint = nullptr
)
{
    QJsonObject object;
    object.insert(QStringLiteral("event"), event);
    if (!role.isEmpty()) object.insert(QStringLiteral("role"), role);
    if (!error.isEmpty()) object.insert(QStringLiteral("error"), error);
    if (!candidateTypes.empty()) {
        QJsonArray values;
        for (const std::string &type : candidateTypes) {
            values.push_back(QString::fromStdString(type));
        }
        object.insert(QStringLiteral("candidateTypes"), values);
    }
    if (source) {
        object.insert(
            QStringLiteral("accessUnits"),
            static_cast<double>(source->emittedAccessUnits)
        );
        object.insert(
            QStringLiteral("keyframes"),
            static_cast<double>(source->emittedKeyframes)
        );
        object.insert(
            QStringLiteral("sourceDrops"),
            static_cast<double>(source->droppedAccessUnits)
        );
    }
    if (endpoint) {
        object.insert(
            QStringLiteral("sentAccessUnits"),
            static_cast<double>(endpoint->sentAccessUnits)
        );
        object.insert(
            QStringLiteral("transportDrops"),
            static_cast<double>(endpoint->droppedAccessUnits)
        );
        object.insert(
            QStringLiteral("sendFailures"),
            static_cast<double>(endpoint->sendFailures)
        );
    }
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact)
                        << Qt::endl;
}

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
    LocatedPackage *located
)
{
    QDeadlineTimer deadline(static_cast<qint64>(timeout.count()));
    SessionError error = SessionError::NotFound;
    while (!deadline.hasExpired()) {
        error = findSingleValid(store, role, sessionId, located);
        if (error != SessionError::NotFound) return error;
        QThread::msleep(50);
    }
    return error;
}

QString discoverRepositoryRoot()
{
    QString root = SessionPackageStore::discoverRepositoryRoot(QDir::currentPath());
    if (!root.isEmpty()) return root;
    return SessionPackageStore::discoverRepositoryRoot(
        QCoreApplication::applicationDirPath()
    );
}

int runPublisher(
    SignalingRole signalingRole,
    std::chrono::milliseconds timeout
)
{
    const QString roleName = signalingRole == SignalingRole::Offerer
                                 ? QStringLiteral("offer")
                                 : QStringLiteral("answer");
    const QString repositoryRoot = discoverRepositoryRoot();
    if (repositoryRoot.isEmpty()) {
        emitEvent(QStringLiteral("failed"), roleName, QStringLiteral("unsafe_path"));
        return 3;
    }
    SessionPackageStore store(
        SessionPackageStore::exchangeRootForRepository(repositoryRoot)
    );
    const SessionError prepareError = store.prepare();
    if (prepareError != SessionError::None) {
        emitEvent(
            QStringLiteral("failed"), roleName,
            SessionPackageCodec::errorName(prepareError)
        );
        return 3;
    }
    const QString samplePath = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("webrtc-assets/sample.mp4"));
    if (!QFileInfo(samplePath).isFile()) {
        emitEvent(
            QStringLiteral("failed"), roleName, QStringLiteral("file_not_found")
        );
        return 4;
    }

    WebRtcSessionConfig configuration;
    configuration.signalingRole = signalingRole;
    configuration.videoDirection = VideoDirection::SendOnly;
    WebRtcEndpointSession endpoint(configuration);
    EndpointConnectionResult connected;

    if (signalingRole == SignalingRole::Offerer) {
        const EndpointDescriptionResult offer = endpoint.createOffer(timeout);
        if (!offer.ok()) {
            emitEvent(
                QStringLiteral("failed"), roleName,
                QString::fromLatin1(WebRtcEndpointSession::errorName(offer.error))
            );
            return 4;
        }
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer, QString::fromStdString(offer.sdp)
        );
        const SessionFileResult written = store.write(package);
        if (!written.ok()) {
            endpoint.close();
            emitEvent(
                QStringLiteral("failed"), roleName,
                SessionPackageCodec::errorName(written.error)
            );
            return 3;
        }
        emitEvent(QStringLiteral("description_exported"), roleName);
        LocatedPackage answer;
        const SessionError answerError = waitForPackage(
            store, SessionRole::Answer, package.sessionId, timeout, &answer
        );
        if (answerError != SessionError::None) {
            (void)store.remove(written.filePath);
            endpoint.close();
            emitEvent(
                QStringLiteral("failed"), roleName,
                SessionPackageCodec::errorName(answerError)
            );
            return 3;
        }
        connected = endpoint.acceptAnswerAndWait(
            answer.package.sdp.toStdString(), timeout
        );
        (void)store.remove(answer.path);
        (void)store.remove(written.filePath);
    } else {
        LocatedPackage offer;
        const SessionError offerError = waitForPackage(
            store, SessionRole::Offer, {}, timeout, &offer
        );
        if (offerError != SessionError::None) {
            emitEvent(
                QStringLiteral("failed"), roleName,
                SessionPackageCodec::errorName(offerError)
            );
            return 3;
        }
        const EndpointDescriptionResult answer =
            endpoint.acceptOfferAndCreateAnswer(
                offer.package.sdp.toStdString(), timeout
            );
        if (!answer.ok()) {
            endpoint.close();
            emitEvent(
                QStringLiteral("failed"), roleName,
                QString::fromLatin1(WebRtcEndpointSession::errorName(answer.error))
            );
            return 4;
        }
        if (store.remove(offer.path) != SessionError::None) {
            endpoint.close();
            emitEvent(QStringLiteral("failed"), roleName, QStringLiteral("io_failure"));
            return 3;
        }
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Answer,
            QString::fromStdString(answer.sdp),
            QDateTime::currentDateTimeUtc(),
            offer.package.sessionId
        );
        const SessionFileResult written = store.write(package);
        if (!written.ok()) {
            endpoint.close();
            emitEvent(
                QStringLiteral("failed"), roleName,
                SessionPackageCodec::errorName(written.error)
            );
            return 3;
        }
        emitEvent(QStringLiteral("description_exported"), roleName);
        connected = endpoint.waitConnected(timeout);
        (void)store.remove(written.filePath);
    }

    if (!connected.ok()) {
        endpoint.close();
        emitEvent(
            QStringLiteral("failed"), roleName,
            QString::fromLatin1(WebRtcEndpointSession::errorName(connected.error))
        );
        return 4;
    }
    emitEvent(
        QStringLiteral("connected"), roleName, {}, connected.candidateTypes
    );

    auto submitPort = endpoint.createSendPort();
    if (!submitPort.has_value()) {
        endpoint.close();
        emitEvent(QStringLiteral("failed"), roleName, QStringLiteral("invalid_state"));
        return 4;
    }
    Mp4H264PublisherSource source;
    const PublisherSourceError startError = source.start(
        QFileInfo(samplePath).absoluteFilePath().toStdString(),
        std::move(*submitPort)
    );
    if (startError != PublisherSourceError::None) {
        endpoint.beginClose();
        source.stop();
        endpoint.close();
        emitEvent(
            QStringLiteral("failed"), roleName,
            QString::fromLatin1(Mp4H264PublisherSource::errorName(startError))
        );
        return 4;
    }
    emitEvent(QStringLiteral("publishing"), roleName);
    const PublisherSourceError sourceError = source.waitForCompletion(timeout);
    endpoint.beginClose();
    source.stop();
    endpoint.close();

    const PublisherSourceSnapshot sourceSnapshot = source.snapshot();
    const EndpointSnapshot endpointSnapshot = endpoint.snapshot();
    if (sourceError != PublisherSourceError::None) {
        emitEvent(
            QStringLiteral("failed"), roleName,
            QString::fromLatin1(Mp4H264PublisherSource::errorName(sourceError)),
            {}, &sourceSnapshot, &endpointSnapshot
        );
        return 4;
    }
    emitEvent(
        QStringLiteral("completed"), roleName, {}, {},
        &sourceSnapshot, &endpointSnapshot
    );
    return endpointSnapshot.sendFailures == 0 ? 0 : 4;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rtmp_monitor_webrtc_client"));
    rtc::InitLogger(rtc::LogLevel::None);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("WebRTC V2 Week 4 headless MP4 publisher")
    );
    parser.addHelpOption();
    parser.addOption({QStringLiteral("media-role"), QStringLiteral("publisher"), QStringLiteral("role")});
    parser.addOption({QStringLiteral("signaling-role"), QStringLiteral("offer or answer"), QStringLiteral("role")});
    parser.addOption({QStringLiteral("source"), QStringLiteral("sample"), QStringLiteral("source")});
    parser.addOption({QStringLiteral("timeout-ms"), QStringLiteral("1000..600000"), QStringLiteral("milliseconds"), QStringLiteral("30000")});

    if (!parser.parse(QCoreApplication::arguments())) {
        emitEvent(QStringLiteral("invalid_arguments"), {}, QStringLiteral("invalid_arguments"));
        return 2;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        QString helpText = parser.helpText();
        const QString invokedAs = QCoreApplication::arguments().constFirst();
        helpText.replace(invokedAs, QCoreApplication::applicationName());
        QTextStream(stdout) << helpText;
        return 0;
    }
    const QString mediaRole = parser.value(QStringLiteral("media-role")).trimmed().toLower();
    const QString signalingRole = parser.value(QStringLiteral("signaling-role")).trimmed().toLower();
    const QString source = parser.value(QStringLiteral("source")).trimmed().toLower();
    bool timeoutOk = false;
    const int timeoutMs = parser.value(QStringLiteral("timeout-ms")).toInt(&timeoutOk);
    if (mediaRole != QStringLiteral("publisher") ||
        source != QStringLiteral("sample") ||
        !QStringList {QStringLiteral("offer"), QStringLiteral("answer")}.contains(signalingRole) ||
        !timeoutOk || timeoutMs < 1000 || timeoutMs > 600000 ||
        !parser.positionalArguments().isEmpty()) {
        emitEvent(QStringLiteral("invalid_arguments"), {}, QStringLiteral("invalid_arguments"));
        return 2;
    }

    int result = 4;
    {
        result = runPublisher(
            signalingRole == QStringLiteral("offer")
                ? SignalingRole::Offerer
                : SignalingRole::Answerer,
            std::chrono::milliseconds(timeoutMs)
        );
    }
    try {
        auto cleanup = rtc::Cleanup();
        if (cleanup.wait_for(std::chrono::seconds(10)) ==
            std::future_status::timeout) {
            emitEvent(QStringLiteral("cleanup_failed"), {}, QStringLiteral("cleanup_timeout"));
            return 4;
        }
        cleanup.get();
    } catch (...) {
        emitEvent(QStringLiteral("cleanup_failed"), {}, QStringLiteral("library_failure"));
        return 4;
    }
    return result;
}
