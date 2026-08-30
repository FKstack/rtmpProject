#include "webrtc_client/WebRtcClientOptions.h"
#include "webrtc_client/WebRtcIceRuntimeConfigLoader.h"

#include <QCommandLineParser>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

using namespace rtmp_monitor::webrtc_client;

namespace {

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
}

std::optional<WebRtcClientOptions> parseOptions(const QStringList &arguments)
{
    QCommandLineParser parser;
    WebRtcClientOptions::configureParser(parser);
    if (!parser.parse(arguments)) return std::nullopt;
    return WebRtcClientOptions::fromParser(parser);
}

} // namespace

class WebRtcClientIceConfigTest final : public QObject
{
    Q_OBJECT

private slots:
    void hostModeIsTheCompatibleDefault()
    {
        const auto options = parseOptions({
            QStringLiteral("client"),
            QStringLiteral("--media-role"), QStringLiteral("publisher"),
            QStringLiteral("--signaling-role"), QStringLiteral("offer"),
            QStringLiteral("--source"), QStringLiteral("sample")
        });
        QVERIFY(options.has_value());
        QCOMPARE(options->iceMode, ClientIceMode::HostOnly);
        QCOMPARE(iceModeName(options->iceMode), QStringLiteral("host"));
    }

    void explicitStunModeIsAccepted()
    {
        const auto options = parseOptions({
            QStringLiteral("client"),
            QStringLiteral("--media-role=viewer"),
            QStringLiteral("--signaling-role=answer"),
            QStringLiteral("--ice-mode=stun")
        });
        QVERIFY(options.has_value());
        QCOMPARE(options->iceMode, ClientIceMode::Stun);
        QVERIFY(!parseOptions({
            QStringLiteral("client"),
            QStringLiteral("--media-role=viewer"),
            QStringLiteral("--signaling-role=answer"),
            QStringLiteral("--ice-mode=relay")
        }).has_value());
    }

    void cameraArgumentsArePublisherOnlyAndBounded()
    {
        const auto camera = parseOptions({
            QStringLiteral("client"),
            QStringLiteral("--media-role=publisher"),
            QStringLiteral("--signaling-role=offer"),
            QStringLiteral("--source=camera"),
            QStringLiteral("--camera-index=2")
        });
        QVERIFY(camera.has_value());
        QCOMPARE(camera->publisherSource, ClientPublisherSource::Camera);
        QCOMPARE(camera->cameraIndex, std::uint32_t(2));
        QVERIFY(!parseOptions({
            QStringLiteral("client"),
            QStringLiteral("--media-role=viewer"),
            QStringLiteral("--signaling-role=answer"),
            QStringLiteral("--camera-index=0")
        }).has_value());
        const auto list = parseOptions({
            QStringLiteral("client"), QStringLiteral("--list-cameras")
        });
        QVERIFY(list.has_value());
        QVERIFY(list->listCameras);
    }

    void validFixedSchemaProducesOneStunServer()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QStringLiteral("ice.json"));
        writeBytes(
            path,
            R"({"schemaVersion":1,"stunUrl":"stun:127.0.0.1:3478"})"
        );
        const IceConfigLoadResult result =
            WebRtcIceRuntimeConfigLoader::load(path);
        QVERIFY(result.ok());
        QCOMPARE(result.configuration.servers.size(), std::size_t(1));
        QCOMPARE(result.configuration.servers.front().urls.size(), std::size_t(1));
        QVERIFY(result.configuration.servers.front().username.empty());
        QVERIFY(result.configuration.servers.front().password.empty());
    }

    void invalidInputsAreClassified_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<IceConfigLoadError>("error");
        QTest::newRow("empty") << QByteArray()
            << IceConfigLoadError::InvalidConfiguration;
        QTest::newRow("invalid-json") << QByteArray("{")
            << IceConfigLoadError::InvalidConfiguration;
        QTest::newRow("wrong-version")
            << QByteArray(R"({"schemaVersion":2,"stunUrl":"stun:127.0.0.1:9"})")
            << IceConfigLoadError::UnsupportedVersion;
        QTest::newRow("extra-field")
            << QByteArray(R"({"schemaVersion":1,"stunUrl":"stun:127.0.0.1:9","username":"x"})")
            << IceConfigLoadError::InvalidConfiguration;
        QTest::newRow("turn")
            << QByteArray(R"({"schemaVersion":1,"stunUrl":"turn:127.0.0.1:9"})")
            << IceConfigLoadError::InvalidConfiguration;
        QTest::newRow("placeholder")
            << QByteArray(R"({"schemaVersion":1,"stunUrl":"stun:<stun-host>:3478"})")
            << IceConfigLoadError::InvalidConfiguration;
        QTest::newRow("whitespace")
            << QByteArray(R"({"schemaVersion":1,"stunUrl":"stun:host name:3478"})")
            << IceConfigLoadError::InvalidConfiguration;
        QTest::newRow("bad-port")
            << QByteArray(R"({"schemaVersion":1,"stunUrl":"stun:127.0.0.1:not-a-port"})")
            << IceConfigLoadError::InvalidConfiguration;
    }

    void invalidInputsAreClassified()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(IceConfigLoadError, error);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QStringLiteral("ice.json"));
        writeBytes(path, bytes);
        QCOMPARE(WebRtcIceRuntimeConfigLoader::load(path).error, error);
    }

    void missingAndUnreadablePathsAreDistinct()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QCOMPARE(
            WebRtcIceRuntimeConfigLoader::load(
                temporary.filePath(QStringLiteral("missing.json"))
            ).error,
            IceConfigLoadError::NotFound
        );
        QCOMPARE(
            WebRtcIceRuntimeConfigLoader::load(temporary.path()).error,
            IceConfigLoadError::ReadFailed
        );
    }
};

QTEST_GUILESS_MAIN(WebRtcClientIceConfigTest)
#include "WebRtcClientIceConfigTest.moc"
