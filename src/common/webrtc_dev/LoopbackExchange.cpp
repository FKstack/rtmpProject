#include "webrtc_dev/LoopbackExchange.h"

#include <QDateTime>

#include <algorithm>

namespace rtmp_monitor::webrtc_dev {
namespace {

bool hostOnly(const QStringList &types)
{
    return !types.isEmpty() &&
           std::all_of(
               types.cbegin(), types.cend(),
               [](const QString &type) { return type == QStringLiteral("host"); }
           );
}

} // namespace

LoopbackResult runLoopbackExchange(SessionPackageStore &store, int cycles)
{
    LoopbackResult aggregate;
    if (cycles < 1) {
        aggregate.probeError = ProbeError::InvalidState;
        return aggregate;
    }
    const SessionError prepareError = store.prepare();
    if (prepareError != SessionError::None) {
        aggregate.sessionError = prepareError;
        return aggregate;
    }

    for (int cycle = 0; cycle < cycles; ++cycle) {
        PeerConnectionProbe offerer;
        PeerConnectionProbe answerer;
        QString offerPath;
        QString answerPath;
        const auto cleanup = [&] {
            if (!offerPath.isEmpty()) (void)store.remove(offerPath);
            if (!answerPath.isEmpty()) (void)store.remove(answerPath);
            offerer.close();
            offerer.close();
            answerer.close();
            answerer.close();
        };

        const ProbeDescriptionResult offer = offerer.createOffer();
        if (!offer.ok() || !hostOnly(offer.candidateTypes)) {
            aggregate.probeError = offer.ok()
                ? ProbeError::ConnectionFailed : offer.error;
            cleanup();
            return aggregate;
        }
        const SessionPackage offerPackage = SessionPackageCodec::create(
            SessionRole::Offer, offer.sdp
        );
        const SessionFileResult offerWrite = store.write(offerPackage);
        if (!offerWrite.ok()) {
            aggregate.sessionError = offerWrite.error;
            cleanup();
            return aggregate;
        }
        offerPath = offerWrite.filePath;

        const SessionFileResult offerRead = store.read(offerPath);
        if (!offerRead.ok()) {
            aggregate.sessionError = offerRead.error;
            cleanup();
            return aggregate;
        }
        const SessionResult decodedOffer =
            SessionPackageCodec::decodeAndValidate(
                offerRead.bytes,
                QDateTime::currentDateTimeUtc(),
                SessionExpectation {SessionRole::Offer, {}, false}
            );
        if (!decodedOffer.ok()) {
            aggregate.sessionError = decodedOffer.error;
            cleanup();
            return aggregate;
        }

        const ProbeDescriptionResult answer = answerer.createAnswer(
            decodedOffer.package->sdp
        );
        if (!answer.ok() || !hostOnly(answer.candidateTypes)) {
            aggregate.probeError = answer.ok()
                ? ProbeError::ConnectionFailed : answer.error;
            cleanup();
            return aggregate;
        }
        if (store.remove(offerPath) != SessionError::None) {
            aggregate.sessionError = SessionError::IoFailure;
            cleanup();
            return aggregate;
        }
        offerPath.clear();

        SessionPackage answerPackage = SessionPackageCodec::create(
            SessionRole::Answer,
            answer.sdp,
            QDateTime::currentDateTimeUtc(),
            offerPackage.sessionId
        );
        const SessionFileResult answerWrite = store.write(answerPackage);
        if (!answerWrite.ok()) {
            aggregate.sessionError = answerWrite.error;
            cleanup();
            return aggregate;
        }
        answerPath = answerWrite.filePath;

        const SessionFileResult answerRead = store.read(answerPath);
        if (!answerRead.ok()) {
            aggregate.sessionError = answerRead.error;
            cleanup();
            return aggregate;
        }
        const SessionResult decodedAnswer =
            SessionPackageCodec::decodeAndValidate(
                answerRead.bytes,
                QDateTime::currentDateTimeUtc(),
                SessionExpectation {
                    SessionRole::Answer,
                    offerPackage.sessionId,
                    false,
                }
            );
        if (!decodedAnswer.ok()) {
            aggregate.sessionError = decodedAnswer.error;
            cleanup();
            return aggregate;
        }

        const ProbeConnectionResult offerConnected =
            offerer.applyAnswerAndWait(decodedAnswer.package->sdp);
        const ProbeConnectionResult answerConnected = answerer.waitConnected();
        if (!offerConnected.ok() || !answerConnected.ok()) {
            aggregate.probeError = !offerConnected.ok()
                ? offerConnected.error : answerConnected.error;
            cleanup();
            return aggregate;
        }
        if (store.remove(answerPath) != SessionError::None) {
            aggregate.sessionError = SessionError::IoFailure;
            cleanup();
            return aggregate;
        }
        answerPath.clear();

        aggregate.candidateTypes = offer.candidateTypes;
        for (const QString &type : answer.candidateTypes) {
            if (!aggregate.candidateTypes.contains(type)) {
                aggregate.candidateTypes.push_back(type);
            }
        }
        std::sort(
            aggregate.candidateTypes.begin(), aggregate.candidateTypes.end()
        );
        ++aggregate.completedCycles;
        cleanup();
    }
    return aggregate;
}

} // namespace rtmp_monitor::webrtc_dev
