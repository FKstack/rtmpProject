#include "webrtc_dev/LoopbackExchange.h"
#include "webrtc_dev/PeerConnectionProbe.h"
#include "webrtc_dev/SessionPackage.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>

#include <chrono>
#include <optional>

using namespace rtmp_monitor::webrtc_dev;

namespace {

struct LocatedPackage
{
    QString filePath;
    SessionPackage package;
    qsizetype byteCount = 0;
};

void emitEvent(
    const QString &event,
    std::optional<SessionRole> role = std::nullopt,
    const QString &sessionId = {},
    qsizetype byteCount = 0,
    const QStringList &candidateTypes = {},
    const QString &error = {},
    qint64 durationMs = -1,
    int count = -1
)
{
    QJsonObject object;
    object.insert(QStringLiteral("event"), event);
    if (role.has_value()) {
        object.insert(
            QStringLiteral("role"), SessionPackageCodec::roleName(*role)
        );
    }
    if (!sessionId.isEmpty()) {
        object.insert(
            QStringLiteral("session"),
            SessionPackageCodec::redactedSessionId(sessionId)
        );
    }
    if (byteCount > 0) {
        object.insert(QStringLiteral("bytes"), static_cast<double>(byteCount));
    }
    if (!candidateTypes.isEmpty()) {
        QJsonArray values;
        for (const QString &type : candidateTypes) values.push_back(type);
        object.insert(QStringLiteral("candidateTypes"), values);
    }
    if (!error.isEmpty()) object.insert(QStringLiteral("error"), error);
    if (durationMs >= 0) {
        object.insert(QStringLiteral("durationMs"), durationMs);
    }
    if (count >= 0) object.insert(QStringLiteral("count"), count);
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact)
                        << Qt::endl;
}

SessionError findSingleValid(
    SessionPackageStore &store,
    SessionRole role,
    const QString &expectedSessionId,
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
            SessionExpectation {role, expectedSessionId, false}
        );
        if (decoded.ok()) {
            valid.push_back({path, *decoded.package, read.byteCount});
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
    *located = valid.front();
    return SessionError::None;
}

int runOffer(SessionPackageStore &store)
{
    PeerConnectionProbe probe;
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    const ProbeDescriptionResult description = probe.createOffer();
    if (!description.ok()) {
        emitEvent(
            QStringLiteral("offer_failed"), SessionRole::Offer, {}, 0, {},
            PeerConnectionProbe::errorName(description.error)
        );
        return 4;
    }

    const SessionPackage package = SessionPackageCodec::create(
        SessionRole::Offer, description.sdp
    );
    const SessionFileResult written = store.write(package);
    if (!written.ok()) {
        emitEvent(
            QStringLiteral("offer_failed"), SessionRole::Offer,
            package.sessionId, 0, {},
            SessionPackageCodec::errorName(written.error)
        );
        probe.close();
        return 3;
    }
    emitEvent(
        QStringLiteral("description_exported"), SessionRole::Offer,
        package.sessionId, written.byteCount, description.candidateTypes
    );

    LocatedPackage answer;
    SessionError findError = SessionError::NotFound;
    QDeadlineTimer deadline(kSessionLifetimeMs);
    while (!deadline.hasExpired()) {
        findError = findSingleValid(
            store, SessionRole::Answer, package.sessionId, &answer
        );
        if (findError != SessionError::NotFound) break;
        QThread::msleep(100);
    }
    if (findError != SessionError::None) {
        emitEvent(
            QStringLiteral("answer_import_failed"), SessionRole::Answer,
            package.sessionId, 0, {},
            SessionPackageCodec::errorName(findError)
        );
        (void)store.remove(written.filePath);
        probe.close();
        return 3;
    }

    const ProbeConnectionResult connected = probe.applyAnswerAndWait(
        answer.package.sdp
    );
    if (!connected.ok()) {
        emitEvent(
            QStringLiteral("connection_failed"), SessionRole::Offer,
            package.sessionId, answer.byteCount, {},
            PeerConnectionProbe::errorName(connected.error)
        );
        (void)store.remove(answer.filePath);
        (void)store.remove(written.filePath);
        probe.close();
        return 4;
    }

    (void)store.remove(answer.filePath);
    (void)store.remove(written.filePath);
    emitEvent(
        QStringLiteral("connected"), SessionRole::Offer, package.sessionId,
        answer.byteCount, connected.candidateTypes, {},
        QDateTime::currentMSecsSinceEpoch() - started
    );
    probe.close();
    probe.close();
    return 0;
}

int runAnswer(SessionPackageStore &store)
{
    LocatedPackage offer;
    const SessionError findError = findSingleValid(
        store, SessionRole::Offer, {}, &offer
    );
    if (findError != SessionError::None) {
        emitEvent(
            QStringLiteral("offer_import_failed"), SessionRole::Offer, {}, 0,
            {}, SessionPackageCodec::errorName(findError)
        );
        return 3;
    }

    PeerConnectionProbe probe;
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    const ProbeDescriptionResult description = probe.createAnswer(
        offer.package.sdp
    );
    if (!description.ok()) {
        emitEvent(
            QStringLiteral("answer_failed"), SessionRole::Answer,
            offer.package.sessionId, offer.byteCount, {},
            PeerConnectionProbe::errorName(description.error)
        );
        probe.close();
        return 4;
    }

    if (store.remove(offer.filePath) != SessionError::None) {
        emitEvent(
            QStringLiteral("offer_cleanup_failed"), SessionRole::Offer,
            offer.package.sessionId, offer.byteCount, {},
            SessionPackageCodec::errorName(SessionError::IoFailure)
        );
        probe.close();
        return 3;
    }

    const SessionPackage answer = SessionPackageCodec::create(
        SessionRole::Answer,
        description.sdp,
        QDateTime::currentDateTimeUtc(),
        offer.package.sessionId
    );
    const SessionFileResult written = store.write(answer);
    if (!written.ok()) {
        emitEvent(
            QStringLiteral("answer_failed"), SessionRole::Answer,
            answer.sessionId, 0, {},
            SessionPackageCodec::errorName(written.error)
        );
        probe.close();
        return 3;
    }
    emitEvent(
        QStringLiteral("description_exported"), SessionRole::Answer,
        answer.sessionId, written.byteCount, description.candidateTypes
    );

    const ProbeConnectionResult connected = probe.waitConnected(
        std::chrono::milliseconds(kSessionLifetimeMs)
    );
    (void)store.remove(written.filePath);
    if (!connected.ok()) {
        emitEvent(
            QStringLiteral("connection_failed"), SessionRole::Answer,
            answer.sessionId, written.byteCount, {},
            PeerConnectionProbe::errorName(connected.error)
        );
        probe.close();
        return 4;
    }
    emitEvent(
        QStringLiteral("connected"), SessionRole::Answer, answer.sessionId,
        written.byteCount, connected.candidateTypes, {},
        QDateTime::currentMSecsSinceEpoch() - started
    );
    probe.close();
    probe.close();
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rtmp_monitor_webrtc_probe"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Developer-only WebRTC Week 2 signaling probe")
    );
    parser.addHelpOption();
    const QCommandLineOption modeOption(
        QStringLiteral("mode"),
        QStringLiteral("offer, answer, loopback, or cleanup"),
        QStringLiteral("mode")
    );
    parser.addOption(modeOption);
    parser.process(application);

    const QString mode = parser.value(modeOption).trimmed().toLower();
    if (!QStringList {
             QStringLiteral("offer"),
             QStringLiteral("answer"),
             QStringLiteral("loopback"),
             QStringLiteral("cleanup"),
         }.contains(mode)) {
        emitEvent(
            QStringLiteral("invalid_mode"), std::nullopt, {}, 0, {},
            QStringLiteral("invalid_mode")
        );
        return 2;
    }

    const QString repositoryRoot =
        SessionPackageStore::discoverRepositoryRoot(QDir::currentPath());
    if (repositoryRoot.isEmpty()) {
        emitEvent(
            QStringLiteral("repository_not_found"), std::nullopt, {}, 0, {},
            QStringLiteral("unsafe_path")
        );
        return 3;
    }
    SessionPackageStore store(
        SessionPackageStore::exchangeRootForRepository(repositoryRoot)
    );
    const SessionError prepareError = store.prepare();
    if (prepareError != SessionError::None) {
        emitEvent(
            QStringLiteral("session_store_failed"), std::nullopt, {}, 0, {},
            SessionPackageCodec::errorName(prepareError)
        );
        return 3;
    }

    int removed = 0;
    const SessionError cleanupError = store.cleanupExpired(
        QDateTime::currentDateTimeUtc(), &removed
    );
    if (cleanupError != SessionError::None) {
        emitEvent(
            QStringLiteral("cleanup_failed"), std::nullopt, {}, 0, {},
            SessionPackageCodec::errorName(cleanupError)
        );
        return 3;
    }
    if (mode == QStringLiteral("cleanup")) {
        emitEvent(
            QStringLiteral("cleanup_complete"), std::nullopt, {}, 0, {}, {},
            -1, removed
        );
        return 0;
    }
    if (mode == QStringLiteral("offer")) return runOffer(store);
    if (mode == QStringLiteral("answer")) return runAnswer(store);

    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    const LoopbackResult result = runLoopbackExchange(store, 1);
    if (!result.ok()) {
        emitEvent(
            QStringLiteral("loopback_failed"), std::nullopt, {}, 0, {},
            result.sessionError != SessionError::None
                ? SessionPackageCodec::errorName(result.sessionError)
                : PeerConnectionProbe::errorName(result.probeError)
        );
        return result.sessionError != SessionError::None ? 3 : 4;
    }
    emitEvent(
        QStringLiteral("loopback_connected"), std::nullopt, {}, 0,
        result.candidateTypes, {},
        QDateTime::currentMSecsSinceEpoch() - started,
        result.completedCycles
    );
    return 0;
}
