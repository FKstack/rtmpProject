#include <QtTest>

#include <QTemporaryDir>

#include "webrtc_dev/LoopbackExchange.h"

using namespace rtmp_monitor::webrtc_dev;

class WebRtcLoopbackTest final : public QObject
{
    Q_OBJECT

private slots:
    void tenNonTrickleHostCandidateCyclesCloseIdempotently()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SessionPackageStore store(directory.filePath(QStringLiteral("exchange")));
        const LoopbackResult result = runLoopbackExchange(store, 10);
        QVERIFY(result.ok());
        QVERIFY(result.completedCycles == 10);
        QVERIFY(!result.candidateTypes.isEmpty());
        for (const QString &type : result.candidateTypes) {
            QVERIFY(type == QStringLiteral("host"));
        }
        QVERIFY(store.managedFiles(SessionRole::Offer).isEmpty());
        QVERIFY(store.managedFiles(SessionRole::Answer).isEmpty());
    }
};

QTEST_GUILESS_MAIN(WebRtcLoopbackTest)
#include "WebRtcLoopbackTest.moc"
