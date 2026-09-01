#include <MQTTAsync.h>
#include <MQTTProperties.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <QUuid>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <atomic>
#include <mutex>

namespace {

constexpr int kSchemaVersion = 1;
constexpr int kMinimumTimeoutMs = 1000;
constexpr int kMaximumTimeoutMs = 15000;
constexpr int kMaximumSecretBytes = 4096;

struct FixtureConfig
{
    QString serverUri;
    QString auditTopic;
    QString caFile;
    QString usernameFile;
    QString passwordFile;
    int timeoutMs = 5000;
};

struct AsyncResult
{
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    bool success = false;
    int reasonCode = 0;
};

struct SessionCallbacks
{
    std::atomic_int unexpectedMessages{0};
    std::atomic_bool connectionLost{false};
};

void complete(AsyncResult *result, bool success, int reasonCode)
{
    if (result == nullptr) return;
    {
        std::lock_guard lock(result->mutex);
        result->done = true;
        result->success = success;
        result->reasonCode = reasonCode;
    }
    result->ready.notify_all();
}

void onSuccess5(void *context, MQTTAsync_successData5 *response)
{
    complete(static_cast<AsyncResult *>(context), true,
             response != nullptr ? response->reasonCode
                                 : MQTTREASONCODE_SUCCESS);
}

void onFailure5(void *context, MQTTAsync_failureData5 *response)
{
    complete(static_cast<AsyncResult *>(context), false,
             response != nullptr ? response->reasonCode : -1);
}

void onConnectionLost(void *context, char *)
{
    auto *callbacks = static_cast<SessionCallbacks *>(context);
    if (callbacks != nullptr) callbacks->connectionLost = true;
}

int onMessageArrived(void *context, char *topicName, int,
                     MQTTAsync_message *message)
{
    auto *callbacks = static_cast<SessionCallbacks *>(context);
    if (callbacks != nullptr) ++callbacks->unexpectedMessages;
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

bool waitFor(AsyncResult &result, int timeoutMs)
{
    std::unique_lock lock(result.mutex);
    return result.ready.wait_for(
               lock, std::chrono::milliseconds(timeoutMs),
               [&result] { return result.done; }) && result.success;
}

QString readSecretFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
        file.size() > kMaximumSecretBytes) {
        if (error != nullptr) *error = QStringLiteral("credential_file_invalid");
        return {};
    }
    const QString value = QString::fromUtf8(file.readAll()).trimmed();
    if (value.isEmpty() || value.contains(QLatin1Char('\n')) ||
        value.contains(QLatin1Char('\r'))) {
        if (error != nullptr) *error = QStringLiteral("credential_value_invalid");
        return {};
    }
    return value;
}

bool isExactAuditTopic(const QString &topic)
{
    if (!topic.startsWith(QStringLiteral("rtmp-monitor/audit/")) ||
        topic.contains(QLatin1Char('#')) || topic.contains(QLatin1Char('+')) ||
        topic.contains(QStringLiteral("device/control"), Qt::CaseInsensitive) ||
        topic.contains(QStringLiteral("device/status"), Qt::CaseInsensitive)) {
        return false;
    }
    const QString suffix = topic.sliced(QStringLiteral("rtmp-monitor/audit/").size());
    if (suffix.size() < 24 || suffix.size() > 64) return false;
    for (const QChar ch : suffix) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('-')) return false;
    }
    return true;
}

bool validateConfig(const QJsonObject &object, const QString &mode,
                    FixtureConfig *config, QString *error)
{
    static const QSet<QString> allowedKeys = {
        QStringLiteral("schemaVersion"), QStringLiteral("serverUri"),
        QStringLiteral("auditTopic"), QStringLiteral("caFile"),
        QStringLiteral("usernameFile"), QStringLiteral("passwordFile"),
        QStringLiteral("timeoutMs")};
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            if (error != nullptr) *error = QStringLiteral("unknown_config_field");
            return false;
        }
    }
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) !=
        kSchemaVersion) {
        if (error != nullptr) *error = QStringLiteral("unsupported_schema");
        return false;
    }

    FixtureConfig value;
    value.serverUri = object.value(QStringLiteral("serverUri")).toString().trimmed();
    value.auditTopic = object.value(QStringLiteral("auditTopic")).toString().trimmed();
    value.caFile = object.value(QStringLiteral("caFile")).toString().trimmed();
    value.usernameFile = object.value(QStringLiteral("usernameFile")).toString().trimmed();
    value.passwordFile = object.value(QStringLiteral("passwordFile")).toString().trimmed();
    value.timeoutMs = object.value(QStringLiteral("timeoutMs")).toInt(5000);

    const QUrl uri(value.serverUri, QUrl::StrictMode);
    if (!uri.isValid() || uri.host().isEmpty() || uri.port() <= 0 ||
        !uri.userInfo().isEmpty() || !uri.path().isEmpty() ||
        uri.hasQuery() || uri.hasFragment() ||
        !isExactAuditTopic(value.auditTopic) ||
        value.timeoutMs < kMinimumTimeoutMs ||
        value.timeoutMs > kMaximumTimeoutMs) {
        if (error != nullptr) *error = QStringLiteral("configuration_invalid");
        return false;
    }

    if (mode == QStringLiteral("legacy-observe")) {
        if (uri.scheme() != QStringLiteral("tcp") || !value.caFile.isEmpty() ||
            !value.usernameFile.isEmpty() || !value.passwordFile.isEmpty()) {
            if (error != nullptr) *error = QStringLiteral("legacy_boundary_invalid");
            return false;
        }
    } else if (mode == QStringLiteral("capability")) {
        if ((uri.scheme() != QStringLiteral("ssl") &&
             uri.scheme() != QStringLiteral("tls")) || value.caFile.isEmpty() ||
            value.usernameFile.isEmpty() || value.passwordFile.isEmpty()) {
            if (error != nullptr) *error = QStringLiteral("capability_boundary_invalid");
            return false;
        }
        for (const QString &path : {value.caFile, value.usernameFile,
                                    value.passwordFile}) {
            if (!QFile::exists(path)) {
                if (error != nullptr) *error = QStringLiteral("required_file_missing");
                return false;
            }
        }
    } else {
        if (error != nullptr) *error = QStringLiteral("unsupported_mode");
        return false;
    }

    if (config != nullptr) *config = value;
    return true;
}

QJsonObject resultObject(const QString &mode, const QString &status,
                         const QString &reason, const QJsonObject &checks)
{
    return {{QStringLiteral("schemaVersion"), kSchemaVersion},
            {QStringLiteral("mode"), mode},
            {QStringLiteral("status"), status},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("checks"), checks}};
}

bool writeResult(const QString &path, const QJsonObject &result, QString *error)
{
    const QByteArray bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
    if (path.isEmpty()) {
        fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
        return true;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit()) {
        if (error != nullptr) *error = QStringLiteral("result_write_failed");
        return false;
    }
    return true;
}

bool runSelfTest(QJsonObject *result)
{
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject legacy{{QStringLiteral("schemaVersion"), kSchemaVersion},
                       {QStringLiteral("serverUri"),
                        QStringLiteral("tcp://127.0.0.1:1883")},
                       {QStringLiteral("auditTopic"),
                        QStringLiteral("rtmp-monitor/audit/") + suffix},
                       {QStringLiteral("timeoutMs"), 1000}};
    FixtureConfig parsed;
    QString error;
    const bool validLegacy = validateConfig(
        legacy, QStringLiteral("legacy-observe"), &parsed, &error);

    QJsonObject wildcard = legacy;
    wildcard[QStringLiteral("auditTopic")] = QStringLiteral("rtmp-monitor/audit/#");
    const bool rejectsWildcard = !validateConfig(
        wildcard, QStringLiteral("legacy-observe"), nullptr, &error);

    QJsonObject control = legacy;
    control[QStringLiteral("auditTopic")] = QStringLiteral("device/control");
    const bool rejectsControl = !validateConfig(
        control, QStringLiteral("legacy-observe"), nullptr, &error);

    QJsonObject credentials = legacy;
    credentials[QStringLiteral("usernameFile")] = QStringLiteral("secret.txt");
    const bool rejectsLegacyCredentials = !validateConfig(
        credentials, QStringLiteral("legacy-observe"), nullptr, &error);

    QJsonObject unknown = legacy;
    unknown[QStringLiteral("endpointDefault")] = QStringLiteral("forbidden");
    const bool rejectsUnknown = !validateConfig(
        unknown, QStringLiteral("legacy-observe"), nullptr, &error);

    const bool passed = validLegacy && rejectsWildcard && rejectsControl &&
                        rejectsLegacyCredentials && rejectsUnknown;
    *result = resultObject(
        QStringLiteral("self-test"),
        passed ? QStringLiteral("passed") : QStringLiteral("failed"),
        passed ? QStringLiteral("self_test_passed")
               : QStringLiteral("self_test_failed"),
        {{QStringLiteral("valid_legacy_config"), validLegacy},
         {QStringLiteral("wildcard_rejected"), rejectsWildcard},
         {QStringLiteral("control_topic_rejected"), rejectsControl},
         {QStringLiteral("legacy_credentials_rejected"), rejectsLegacyCredentials},
         {QStringLiteral("unknown_field_rejected"), rejectsUnknown}});
    return passed;
}

bool runObservation(const FixtureConfig &config, const QString &mode,
                    QJsonObject *result)
{
    const QByteArray uri = config.serverUri.toUtf8();
    const QByteArray topic = config.auditTopic.toUtf8();
    const QByteArray clientId =
        (QStringLiteral("rtmp-monitor-audit-") +
         QUuid::createUuid().toString(QUuid::WithoutBraces)).toUtf8();

    MQTTAsync handle = nullptr;
    MQTTAsync_createOptions createOptions = MQTTAsync_createOptions_initializer5;
    createOptions.sendWhileDisconnected = 0;
    createOptions.maxBufferedMessages = 1;
    createOptions.restoreMessages = 0;
    createOptions.persistQoS0 = 0;
    const int createCode = MQTTAsync_createWithOptions(
        &handle, uri.constData(), clientId.constData(),
        MQTTCLIENT_PERSISTENCE_NONE, nullptr, &createOptions);
    if (createCode != MQTTASYNC_SUCCESS) {
        *result = resultObject(mode, QStringLiteral("failed"),
                               QStringLiteral("client_create_failed"), {});
        return false;
    }

    SessionCallbacks callbacks;
    MQTTAsync_setCallbacks(handle, &callbacks, &onConnectionLost,
                           &onMessageArrived, nullptr);

    QString credentialError;
    const QString username = mode == QStringLiteral("capability")
        ? readSecretFile(config.usernameFile, &credentialError) : QString{};
    const QString password = mode == QStringLiteral("capability")
        ? readSecretFile(config.passwordFile, &credentialError) : QString{};
    if (mode == QStringLiteral("capability") &&
        (username.isEmpty() || password.isEmpty())) {
        MQTTAsync_destroy(&handle);
        *result = resultObject(mode, QStringLiteral("failed"), credentialError, {});
        return false;
    }
    const QByteArray usernameBytes = username.toUtf8();
    const QByteArray passwordBytes = password.toUtf8();
    const QByteArray caBytes = config.caFile.toUtf8();

    MQTTProperties connectProperties = MQTTProperties_initializer;
    MQTTProperty sessionExpiry{};
    sessionExpiry.identifier = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
    sessionExpiry.value.integer4 = 0;
    MQTTProperties_add(&connectProperties, &sessionExpiry);

    MQTTAsync_SSLOptions sslOptions = MQTTAsync_SSLOptions_initializer;
    sslOptions.trustStore = caBytes.isEmpty() ? nullptr : caBytes.constData();
    sslOptions.enableServerCertAuth = 1;
    sslOptions.sslVersion = MQTT_SSL_VERSION_TLS_1_2;
    sslOptions.verify = 1;

    AsyncResult connectResult;
    MQTTAsync_connectOptions connectOptions = MQTTAsync_connectOptions_initializer5;
    connectOptions.keepAliveInterval = 15;
    connectOptions.connectTimeout = qMax(1, config.timeoutMs / 1000);
    connectOptions.cleanstart = 1;
    connectOptions.automaticReconnect = 0;
    connectOptions.username = usernameBytes.isEmpty() ? nullptr
                                                       : usernameBytes.constData();
    connectOptions.password = passwordBytes.isEmpty() ? nullptr
                                                       : passwordBytes.constData();
    connectOptions.ssl = mode == QStringLiteral("capability") ? &sslOptions : nullptr;
    connectOptions.connectProperties = &connectProperties;
    connectOptions.context = &connectResult;
    connectOptions.onSuccess5 = &onSuccess5;
    connectOptions.onFailure5 = &onFailure5;
    const int connectCode = MQTTAsync_connect(handle, &connectOptions);
    const bool connected = connectCode == MQTTASYNC_SUCCESS &&
                           waitFor(connectResult, config.timeoutMs);
    bool subscribed = false;
    bool unsubscribed = false;
    bool disconnected = false;
    AsyncResult subscribeResult;
    AsyncResult unsubscribeResult;
    AsyncResult disconnectResult;
    if (connected) {
        MQTTAsync_responseOptions subscribeOptions =
            MQTTAsync_responseOptions_initializer;
        subscribeOptions.context = &subscribeResult;
        subscribeOptions.onSuccess5 = &onSuccess5;
        subscribeOptions.onFailure5 = &onFailure5;
        const int subscribeCode = MQTTAsync_subscribe(
            handle, topic.constData(), mode == QStringLiteral("capability") ? 1 : 0,
            &subscribeOptions);
        subscribed = subscribeCode == MQTTASYNC_SUCCESS &&
                     waitFor(subscribeResult, config.timeoutMs);

        if (subscribed) {
            QThread::msleep(250);
            MQTTAsync_responseOptions unsubscribeOptions =
                MQTTAsync_responseOptions_initializer;
            unsubscribeOptions.context = &unsubscribeResult;
            unsubscribeOptions.onSuccess5 = &onSuccess5;
            unsubscribeOptions.onFailure5 = &onFailure5;
            const int unsubscribeCode = MQTTAsync_unsubscribe(
                handle, topic.constData(), &unsubscribeOptions);
            unsubscribed = unsubscribeCode == MQTTASYNC_SUCCESS &&
                           waitFor(unsubscribeResult, config.timeoutMs);
        }

        MQTTAsync_disconnectOptions disconnectOptions =
            MQTTAsync_disconnectOptions_initializer5;
        disconnectOptions.timeout = 500;
        disconnectOptions.context = &disconnectResult;
        disconnectOptions.onSuccess5 = &onSuccess5;
        disconnectOptions.onFailure5 = &onFailure5;
        const int disconnectCode = MQTTAsync_disconnect(handle, &disconnectOptions);
        disconnected = disconnectCode == MQTTASYNC_SUCCESS &&
                       waitFor(disconnectResult, config.timeoutMs);
    }
    // destroy() joins Paho workers and prevents a timed-out operation from
    // invoking one of the stack-owned callback contexts after this function
    // returns. Connect properties remain alive until the handle is gone.
    MQTTAsync_destroy(&handle);
    MQTTProperties_free(&connectProperties);

    const bool noMessages = callbacks.unexpectedMessages.load() == 0;
    const bool coreObservationPassed = connected && subscribed && unsubscribed &&
                                       disconnected && noMessages &&
                                       !callbacks.connectionLost.load();
    QJsonObject checks{{QStringLiteral("mqtt5_connected"), connected},
                       {QStringLiteral("exact_topic_subscribed"), subscribed},
                       {QStringLiteral("unsubscribed"), unsubscribed},
                       {QStringLiteral("disconnected"), disconnected},
                       {QStringLiteral("no_payload_observed"), noMessages},
                       {QStringLiteral("connection_stable"),
                        !callbacks.connectionLost.load()}};
    if (mode == QStringLiteral("capability")) {
        checks[QStringLiteral("tls_server_auth_requested")] = true;
        checks[QStringLiteral("session_expiry_zero_requested")] = true;
        checks[QStringLiteral("credential_files_used")] = true;
        checks[QStringLiteral("wrong_ca_rejected")] = false;
        checks[QStringLiteral("wrong_credentials_rejected")] = false;
        checks[QStringLiteral("retained_publish_rejected")] = false;
        checks[QStringLiteral("unauthorized_topic_rejected")] = false;
        checks[QStringLiteral("expiry_and_limits_qualified")] = false;
        *result = resultObject(
            mode, QStringLiteral("blocked"),
            coreObservationPassed
                ? QStringLiteral("full_capability_matrix_required")
                : QStringLiteral("core_capability_observation_failed"),
            checks);
        return false;
    }
    *result = resultObject(
        mode,
        coreObservationPassed ? QStringLiteral("passed")
                              : QStringLiteral("failed"),
        coreObservationPassed ? QStringLiteral("observation_passed")
                              : QStringLiteral("observation_failed"),
        checks);
    return coreObservationPassed;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("P2P Direct Broker Fixture"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("mode"),
                      QStringLiteral("self-test, legacy-observe or capability"),
                      QStringLiteral("mode")});
    parser.addOption({QStringLiteral("config"),
                      QStringLiteral("Ignored runtime JSON configuration"),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("result"),
                      QStringLiteral("Sanitized JSON result path"),
                      QStringLiteral("path")});
    parser.process(app);

    const QString mode = parser.value(QStringLiteral("mode"));
    const QString resultPath = parser.value(QStringLiteral("result"));
    QJsonObject result;
    bool passed = false;
    if (mode == QStringLiteral("self-test")) {
        passed = runSelfTest(&result);
    } else {
        QFile file(parser.value(QStringLiteral("config")));
        QString error;
        if (!file.open(QIODevice::ReadOnly)) {
            result = resultObject(mode, QStringLiteral("failed"),
                                  QStringLiteral("config_read_failed"), {});
        } else {
            QJsonParseError parseError;
            const QJsonDocument document =
                QJsonDocument::fromJson(file.readAll(), &parseError);
            FixtureConfig config;
            if (parseError.error != QJsonParseError::NoError ||
                !document.isObject()) {
                result = resultObject(mode, QStringLiteral("failed"),
                                      QStringLiteral("config_json_invalid"), {});
            } else if (!validateConfig(document.object(), mode, &config, &error)) {
                result = resultObject(mode, QStringLiteral("failed"), error, {});
            } else {
                passed = runObservation(config, mode, &result);
            }
        }
    }

    QString writeError;
    if (!writeResult(resultPath, result, &writeError)) return 3;
    return passed ? 0 : 2;
}
