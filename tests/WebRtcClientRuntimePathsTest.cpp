#include "webrtc_client/WebRtcClientRuntimePaths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace rtmp_monitor::webrtc_client;

namespace {

void touch(const QString &path)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
}

QString makeRepository(QTemporaryDir &temporary)
{
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("repo"));
    QDir().mkpath(QDir(root).filePath(QStringLiteral(".git")));
    touch(QDir(root).filePath(QStringLiteral("CMakeLists.txt")));
    return root;
}

} // namespace

class WebRtcClientRuntimePathsTest final : public QObject
{
    Q_OBJECT

private slots:
    void repositoryLayoutUsesManagedExchangeRoot()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString root = makeRepository(temporary);
        const QString app = QDir(root).filePath(QStringLiteral("out/bin"));
        QVERIFY(QDir().mkpath(app));

        const auto result = WebRtcClientRuntimePaths::resolve(app, root);
        QVERIFY(result.ok());
        QCOMPARE(result.layout, WebRtcClientRuntimeLayout::Repository);
        QCOMPARE(
            QDir::cleanPath(result.exchangeRoot),
            QDir(root).filePath(QStringLiteral("out/webrtc-p2p/session-exchange"))
        );
        QCOMPARE(
            QDir::cleanPath(result.iceConfigPath),
            QDir(root).filePath(
                QStringLiteral("out/webrtc-p2p/local-config/ice-runtime.json")
            )
        );
        QCOMPARE(WebRtcClientRuntimePaths::layoutName(result.layout),
                 QStringLiteral("repository"));
    }

    void portableMarkerHasPriorityOverRepository()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString root = makeRepository(temporary);
        const QString app = QDir(root).filePath(QStringLiteral("package"));
        QVERIFY(QDir().mkpath(app));
        touch(QDir(app).filePath(QStringLiteral("package-manifest.json")));

        const auto first = WebRtcClientRuntimePaths::resolve(app, root);
        const auto second = WebRtcClientRuntimePaths::resolve(app, root);
        QVERIFY(first.ok());
        QCOMPARE(first.layout, WebRtcClientRuntimeLayout::Portable);
        QCOMPARE(first.exchangeRoot, second.exchangeRoot);
        QCOMPARE(
            QDir::cleanPath(first.exchangeRoot),
            QDir(app).filePath(QStringLiteral("session-exchange"))
        );
        QCOMPARE(
            QDir::cleanPath(first.samplePath),
            QDir(app).filePath(QStringLiteral("webrtc-assets/sample.mp4"))
        );
        QCOMPARE(
            QDir::cleanPath(first.iceConfigPath),
            QDir(app).filePath(
                QStringLiteral("local-config/ice-runtime.json")
            )
        );
    }

    void missingLayoutIsRejected()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString app = QDir(temporary.path()).filePath(QStringLiteral("app"));
        const QString cwd = QDir(temporary.path()).filePath(QStringLiteral("cwd"));
        QVERIFY(QDir().mkpath(app));
        QVERIFY(QDir().mkpath(cwd));
        const auto result = WebRtcClientRuntimePaths::resolve(app, cwd);
        QVERIFY(!result.ok());
        QCOMPARE(result.layout, WebRtcClientRuntimeLayout::Invalid);
        QVERIFY(result.exchangeRoot.isEmpty());
        QVERIFY(result.iceConfigPath.isEmpty());
    }
};

QTEST_GUILESS_MAIN(WebRtcClientRuntimePathsTest)
#include "WebRtcClientRuntimePathsTest.moc"
