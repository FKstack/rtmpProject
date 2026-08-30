#include "logging/LogManager.h"
#include "media/MultiStreamPlaybackManager.h"
#include "publisher/Mp4H264PublisherSource.h"
#include "render/RenderTypes.h"
#include "ui/MainWindow.h"
#include "ui/VideoCanvasHost.h"
#include "webrtc_dev/SessionPackage.h"
#include "webrtc_product/WebRtcProductSessionController.h"
#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace rtmp_monitor::publisher;
using namespace rtmp_monitor::webrtc_dev;
using namespace rtmp_monitor::webrtc_product;
using namespace rtmp_monitor::webrtc_transport;

namespace {

constexpr std::int64_t kFrameDurationUs = 33'333;
constexpr std::size_t kMaximumFixtureAccessUnits = 512;
constexpr std::size_t kMaximumFixtureBytes = 64U * 1024U * 1024U;

struct Options
{
    QString scenario = QStringLiteral("four");
    QString samplePath;
    QString stopFile;
    int warmupSeconds = 60;
    int sampleSeconds = 1'800;
    int stopSecond = 600;
    int rebuildSecond = 720;
    bool selfTest = false;
};

struct ParseResult
{
    Options options;
    bool ok = false;
    QString reason;
};

QString optionValue(const QString &argument, const QString &name)
{
    const QString prefix = name + QLatin1Char('=');
    return argument.startsWith(prefix) ? argument.mid(prefix.size()) : QString {};
}

bool parsePositiveInt(const QString &value, int minimum, int *result)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed < minimum) return false;
    *result = parsed;
    return true;
}

ParseResult parseOptions(const QStringList &arguments)
{
    ParseResult result;
    result.ok = true;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--self-test")) {
            result.options.selfTest = true;
            continue;
        }
        if (argument.startsWith(QStringLiteral("--scenario="))) {
            result.options.scenario = optionValue(
                argument, QStringLiteral("--scenario")
            );
            if (result.options.scenario != QStringLiteral("single") &&
                result.options.scenario != QStringLiteral("four")) {
                result.ok = false;
                result.reason = QStringLiteral("invalid_scenario");
                return result;
            }
            continue;
        }
        if (argument.startsWith(QStringLiteral("--sample="))) {
            result.options.samplePath = optionValue(
                argument, QStringLiteral("--sample")
            );
            if (result.options.samplePath.isEmpty()) {
                result.ok = false;
                result.reason = QStringLiteral("sample_required");
                return result;
            }
            continue;
        }
        if (argument.startsWith(QStringLiteral("--stop-file="))) {
            result.options.stopFile = optionValue(
                argument, QStringLiteral("--stop-file")
            );
            if (result.options.stopFile.isEmpty()) {
                result.ok = false;
                result.reason = QStringLiteral("invalid_stop_file");
                return result;
            }
            continue;
        }
        struct IntegerOption
        {
            const char *name;
            int Options::*member;
            int minimum;
        };
        static constexpr IntegerOption integerOptions[] {
            {"--warmup-seconds", &Options::warmupSeconds, 0},
            {"--sample-seconds", &Options::sampleSeconds, 1},
            {"--stop-second", &Options::stopSecond, 1},
            {"--rebuild-second", &Options::rebuildSecond, 2},
        };
        bool matched = false;
        for (const IntegerOption &entry : integerOptions) {
            const QString name = QString::fromLatin1(entry.name);
            if (!argument.startsWith(name + QLatin1Char('='))) continue;
            matched = true;
            if (!parsePositiveInt(
                    optionValue(argument, name), entry.minimum,
                    &(result.options.*(entry.member)))) {
                result.ok = false;
                result.reason = QStringLiteral("invalid_duration");
                return result;
            }
            break;
        }
        if (!matched) {
            result.ok = false;
            result.reason = QStringLiteral("unknown_argument");
            return result;
        }
    }
    if (!result.options.selfTest && result.options.samplePath.isEmpty()) {
        result.ok = false;
        result.reason = QStringLiteral("sample_required");
    } else if (result.options.scenario == QStringLiteral("four") &&
               result.options.rebuildSecond <= result.options.stopSecond) {
        result.ok = false;
        result.reason = QStringLiteral("invalid_fault_window");
    }
    return result;
}

bool containsSensitiveOutput(const QByteArray &payload)
{
    const QString text = QString::fromUtf8(payload);
    static const QRegularExpression forbiddenKey(
        QStringLiteral(
            R"regex("(?:sdp|candidate|iceCredential|fingerprint|deviceIdentifier|absolutePath)"\s*:)regex"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    static const QRegularExpression windowsPath(
        QStringLiteral(R"([A-Za-z]:[\\/])")
    );
    static const QRegularExpression uuid(
        QStringLiteral(
            R"(\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}\b)"
        )
    );
    static const QRegularExpression ipv4(
        QStringLiteral(R"((?:^|["\s])(?:\d{1,3}\.){3}\d{1,3}(?=$|["\s]))")
    );
    return text.contains(QStringLiteral("rtmp://"), Qt::CaseInsensitive) ||
           forbiddenKey.match(text).hasMatch() ||
           windowsPath.match(text).hasMatch() || uuid.match(text).hasMatch() ||
           ipv4.match(text).hasMatch();
}

void emitJson(const QJsonObject &object)
{
    QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (containsSensitiveOutput(payload)) {
        payload = QJsonDocument(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("reason"), QStringLiteral("sensitive_output_blocked")}
        }).toJson(QJsonDocument::Compact);
    }
    std::fwrite(payload.constData(), 1, static_cast<std::size_t>(payload.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

class FixtureCycle final
{
public:
    explicit FixtureCycle(std::vector<H264AccessUnit> units)
        : units_(std::move(units))
    {
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return !units_.empty() && units_.front().keyFrame;
    }

    [[nodiscard]] H264AccessUnit next()
    {
        H264AccessUnit result = units_.at(cursor_);
        result.mediaTimestampUs = nextTimestampUs_;
        nextTimestampUs_ += kFrameDurationUs;
        cursor_ = (cursor_ + 1U) % units_.size();
        return result;
    }

    [[nodiscard]] const std::vector<H264AccessUnit> &units() const noexcept
    {
        return units_;
    }

private:
    std::vector<H264AccessUnit> units_;
    std::size_t cursor_ = 0;
    std::int64_t nextTimestampUs_ = 0;
};

std::optional<std::vector<H264AccessUnit>> loadFixtureCycle(
    const QString &samplePath
)
{
    std::vector<H264AccessUnit> units;
    std::size_t totalBytes = 0;
    bool collecting = false;
    Mp4H264PublisherSource source;
    const PublisherSourceError started = source.start(
        samplePath.toStdString(),
        [&](H264AccessUnit accessUnit) {
            if (!collecting && accessUnit.keyFrame) collecting = true;
            if (!collecting) return H264SubmitResult::Accepted;
            if (units.size() >= kMaximumFixtureAccessUnits ||
                totalBytes + accessUnit.annexB.size() > kMaximumFixtureBytes) {
                return H264SubmitResult::DroppedCapacity;
            }
            totalBytes += accessUnit.annexB.size();
            units.push_back(std::move(accessUnit));
            return H264SubmitResult::Accepted;
        }
    );
    if (started != PublisherSourceError::None) return std::nullopt;
    const PublisherSourceError completed = source.waitForCompletion(
        std::chrono::seconds(90)
    );
    source.stop();
    if (completed != PublisherSourceError::None || units.empty() ||
        !units.front().keyFrame) {
        return std::nullopt;
    }
    return units;
}

class MediaPacer final
{
public:
    explicit MediaPacer(std::vector<H264AccessUnit> units)
        : cycle_(std::move(units))
    {
    }

    ~MediaPacer() { stop(); }

    MediaPacer(const MediaPacer &) = delete;
    MediaPacer &operator=(const MediaPacer &) = delete;

    [[nodiscard]] bool start()
    {
        if (!cycle_.valid() || worker_.joinable()) return false;
        worker_ = std::thread([this] { run(); });
        return true;
    }

    void setPort(int slot, H264SubmitPort port)
    {
        const std::lock_guard lock(mutex_);
        const auto found = std::find_if(
            ports_.begin(), ports_.end(),
            [slot](const PortEntry &entry) { return entry.slot == slot; }
        );
        if (found == ports_.end()) {
            ports_.push_back({slot, std::move(port)});
        } else {
            found->port = std::move(port);
        }
    }

    void removePort(int slot)
    {
        const std::lock_guard lock(mutex_);
        ports_.erase(
            std::remove_if(
                ports_.begin(), ports_.end(),
                [slot](const PortEntry &entry) { return entry.slot == slot; }
            ),
            ports_.end()
        );
    }

    void stop() noexcept
    {
        {
            const std::lock_guard lock(mutex_);
            closing_ = true;
        }
        changed_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    struct PortEntry
    {
        int slot = 0;
        H264SubmitPort port;
    };

    void run()
    {
        auto nextTick = std::chrono::steady_clock::now();
        for (;;) {
            std::vector<PortEntry> ports;
            {
                std::unique_lock lock(mutex_);
                if (changed_.wait_until(
                        lock, nextTick, [this] { return closing_; })) {
                    return;
                }
                ports = ports_;
            }
            const H264AccessUnit accessUnit = cycle_.next();
            for (const PortEntry &entry : ports) {
                (void)entry.port(accessUnit);
            }
            nextTick += std::chrono::microseconds(kFrameDurationUs);
            const auto now = std::chrono::steady_clock::now();
            if (nextTick + std::chrono::milliseconds(100) < now) {
                nextTick = now;
            }
        }
    }

    FixtureCycle cycle_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<PortEntry> ports_;
    bool closing_ = false;
    std::thread worker_;
};

struct LocatedPackage
{
    SessionPackage package;
};

std::optional<LocatedPackage> waitForPackage(
    SessionPackageStore &store,
    SessionRole role,
    const QString &sessionId
)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 20'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        for (const QString &path : store.managedFiles(role)) {
            const SessionFileResult read = store.read(path);
            if (!read.ok()) continue;
            const SessionResult decoded = SessionPackageCodec::decodeAndValidate(
                read.bytes,
                QDateTime::currentDateTimeUtc(),
                SessionExpectation {role, sessionId, false}
            );
            if (decoded.ok()) return LocatedPackage {*decoded.package};
        }
        QThread::msleep(20);
    }
    return std::nullopt;
}

struct Route
{
    int slot = 0;
    StreamId streamId = kInvalidStreamId;
    std::unique_ptr<WebRtcEndpointSession> sender;
    H264SubmitPort port;
    std::uint64_t previousPresented = 0;
    bool active = false;
};

bool startRoute(
    int slot,
    WebRtcProductSessionController &controller,
    Route *route,
    QString *failureReason
)
{
    const auto fail = [&](const QString &reason, StreamId streamId) {
        if (failureReason != nullptr) *failureReason = reason;
        if (streamId != kInvalidStreamId) controller.cancel(streamId);
        return false;
    };
    WebRtcSessionRequest request;
    request.displayName = QStringLiteral("qualification-%1").arg(slot);
    StreamId streamId = kInvalidStreamId;
    QString error;
    if (!controller.start(request, &error, &streamId)) {
        return fail(QStringLiteral("product_start"), streamId);
    }

    WebRtcSessionConfig configuration;
    configuration.signalingRole = SignalingRole::Offerer;
    configuration.videoDirection = VideoDirection::SendOnly;
    auto sender = std::make_unique<WebRtcEndpointSession>(configuration);
    SessionPackageStore store(controller.exchangeRoot(streamId));
    if (store.prepare() != SessionError::None) {
        return fail(QStringLiteral("store_prepare"), streamId);
    }
    const EndpointDescriptionResult offer = sender->createOffer();
    if (!offer.ok()) {
        return fail(QStringLiteral("offer"), streamId);
    }
    const SessionPackage package = SessionPackageCodec::create(
        SessionRole::Offer, QString::fromStdString(offer.sdp)
    );
    if (!store.write(package).ok()) {
        return fail(QStringLiteral("offer_write"), streamId);
    }
    const auto answer = waitForPackage(
        store, SessionRole::Answer, package.sessionId
    );
    if (!answer.has_value()) {
        return fail(QStringLiteral("answer_timeout"), streamId);
    }
    const EndpointConnectionResult connected = sender->acceptAnswerAndWait(
        answer->package.sdp.toStdString()
    );
    if (!connected.ok()) {
        return fail(QStringLiteral("connect"), streamId);
    }
    auto port = sender->createSendPort();
    if (!port.has_value()) {
        return fail(QStringLiteral("send_port"), streamId);
    }
    route->slot = slot;
    route->streamId = streamId;
    route->sender = std::move(sender);
    route->port = std::move(*port);
    route->previousPresented = 0;
    route->active = true;
    return true;
}

void closeRoute(Route *route, WebRtcProductSessionController &controller)
{
    if (!route->active) return;
    controller.cancel(route->streamId);
    if (route->sender) {
        route->sender->beginClose();
        route->sender->close();
    }
    route->active = false;
    route->streamId = kInvalidStreamId;
}

QString phaseName(qint64 elapsedMs, int warmupSeconds)
{
    return elapsedMs < static_cast<qint64>(warmupSeconds) * 1'000
               ? QStringLiteral("warmup")
               : QStringLiteral("measurement");
}

bool submitWasAccepted(H264SubmitResult result)
{
    return result == H264SubmitResult::Accepted ||
           result == H264SubmitResult::AcceptedAfterDrop;
}

bool runSelfTest()
{
    const ParseResult invalid = parseOptions({
        QStringLiteral("runner"), QStringLiteral("--scenario=invalid")
    });
    if (invalid.ok || invalid.reason != QStringLiteral("invalid_scenario")) {
        return false;
    }
    const ParseResult badWindow = parseOptions({
        QStringLiteral("runner"), QStringLiteral("--scenario=four"),
        QStringLiteral("--sample=fixture.mp4"),
        QStringLiteral("--stop-second=9"),
        QStringLiteral("--rebuild-second=8")
    });
    if (badWindow.ok ||
        badWindow.reason != QStringLiteral("invalid_fault_window")) {
        return false;
    }
    H264AccessUnit first;
    first.annexB = {0, 0, 0, 1, 0x65};
    first.mediaTimestampUs = 17;
    first.keyFrame = true;
    H264AccessUnit second = first;
    second.keyFrame = false;
    FixtureCycle cycle({first, second});
    if (!cycle.valid()) return false;
    const H264AccessUnit one = cycle.next();
    const H264AccessUnit two = cycle.next();
    const H264AccessUnit three = cycle.next();
    if (one.mediaTimestampUs != 0 || two.mediaTimestampUs != 33'333 ||
        three.mediaTimestampUs != 66'666 || !one.keyFrame ||
        two.keyFrame || !three.keyFrame ||
        cycle.units().front().mediaTimestampUs != 17) {
        return false;
    }
    const QByteArray safe = QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("sample")},
        {QStringLiteral("streamId"), QStringLiteral("4")}
    }).toJson(QJsonDocument::Compact);
    const QByteArray unsafe = QJsonDocument(QJsonObject {
        {QStringLiteral("candidate"), QStringLiteral("redacted")}
    }).toJson(QJsonDocument::Compact);
    return !containsSensitiveOutput(safe) && containsSensitiveOutput(unsafe) &&
           containsSensitiveOutput("{\"value\":\"rtmp://host/live\"}") &&
           containsSensitiveOutput("{\"value\":\"C:\\\\private\"}");
}

struct ScenarioResult
{
    bool passed = false;
    bool officialDuration = false;
    bool fifthRejected = true;
    bool oldPortRejected = true;
    bool recoveryWithinTenSeconds = true;
    bool continuityPassed = true;
    bool queuesPassed = true;
    bool cleanupPassed = false;
    QString reason = QStringLiteral("none");
};

ScenarioResult runScenario(
    const Options &options,
    std::vector<H264AccessUnit> fixture
)
{
    ScenarioResult result;
    result.officialDuration =
        (options.scenario == QStringLiteral("single") &&
         options.warmupSeconds == 60 && options.sampleSeconds == 600) ||
        (options.scenario == QStringLiteral("four") &&
         options.warmupSeconds == 60 && options.sampleSeconds == 1'800 &&
         options.stopSecond == 600 && options.rebuildSecond == 720);

    QTemporaryDir exchange;
    if (!exchange.isValid()) {
        result.reason = QStringLiteral("exchange_unavailable");
        return result;
    }

    MultiStreamPlaybackManager manager;
    MainWindow window(RendererPreference::Cpu);
    window.resize(960, 640);
    window.show();
    LogManager logs;
    WebRtcProductSessionController controller(
        &window, &manager, &logs, exchange.path()
    );
    const int routeCount = options.scenario == QStringLiteral("single") ? 1 : 4;
    std::vector<Route> routes(static_cast<std::size_t>(routeCount));
    for (int index = 0; index < routeCount; ++index) {
        QString failureReason;
        if (!startRoute(
                index + 1, controller, &routes[index], &failureReason)) {
            result.reason = QStringLiteral("route_start_%1").arg(failureReason);
            for (Route &route : routes) closeRoute(&route, controller);
            controller.cancel();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            result.cleanupPassed = manager.streamCount() == 0 &&
                                   window.videoWidgetCount() == 0 &&
                                   !controller.isActive();
            return result;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QElapsedTimer gridSettle;
        gridSettle.start();
        while (gridSettle.elapsed() < 650) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(10);
        }
    }

    if (routeCount == 4) {
        WebRtcSessionRequest fifth;
        fifth.displayName = QStringLiteral("qualification-capacity");
        StreamId unexpected = kInvalidStreamId;
        QString error;
        result.fifthRejected = !controller.start(fifth, &error, &unexpected);
        if (!result.fifthRejected && unexpected != kInvalidStreamId) {
            controller.cancel(unexpected);
        }
    }

    MediaPacer pacer(std::move(fixture));
    for (const Route &route : routes) pacer.setPort(route.slot, route.port);
    if (!pacer.start()) {
        result.reason = QStringLiteral("pacer_start_failed");
        controller.cancel();
        return result;
    }

    bool stopped = false;
    bool rebuilt = false;
    qint64 rebuiltAtMs = -1;
    qint64 directAtMs = -1;
    H264SubmitPort oldPort;
    std::size_t maximumTransportQueue = 0;
    int maximumDecodeQueue = 0;
    qint64 maximumDecodeBytes = 0;
    QElapsedTimer timer;
    timer.start();
    qint64 nextReportMs = 1'000;
    const qint64 warmupMs = static_cast<qint64>(options.warmupSeconds) * 1'000;
    const qint64 totalMs = warmupMs +
                           static_cast<qint64>(options.sampleSeconds) * 1'000;

    while (timer.elapsed() < totalMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (!options.stopFile.isEmpty() &&
            QFileInfo::exists(options.stopFile)) {
            result.reason = QStringLiteral("stop_requested");
            break;
        }
        const qint64 now = timer.elapsed();
        const qint64 measurementMs = now - warmupMs;
        if (routeCount == 4 && measurementMs >= 0 &&
            !stopped && measurementMs >= options.stopSecond * 1'000LL) {
            Route &fault = routes[1];
            pacer.removePort(fault.slot);
            oldPort = fault.port;
            closeRoute(&fault, controller);
            if (oldPort) {
                H264AccessUnit probe;
                probe.annexB = {0, 0, 0, 1, 0x65};
                probe.keyFrame = true;
                result.oldPortRejected = !submitWasAccepted(oldPort(probe));
            }
            stopped = true;
        }
        if (routeCount == 4 && stopped && !rebuilt &&
            measurementMs >= options.rebuildSecond * 1'000LL) {
            const qint64 rebuildStartedAtMs = timer.elapsed();
            Route replacement;
            QString failureReason;
            if (!startRoute(2, controller, &replacement, &failureReason) ||
                !controller.exchangeRoot(replacement.streamId).endsWith(
                    QStringLiteral("session-02"))) {
                result.reason = QStringLiteral("rebuild_%1").arg(
                    failureReason.isEmpty()
                        ? QStringLiteral("slot") : failureReason
                );
                result.recoveryWithinTenSeconds = false;
                break;
            }
            routes[1] = std::move(replacement);
            pacer.setPort(2, routes[1].port);
            rebuilt = true;
            rebuiltAtMs = rebuildStartedAtMs;
        }

        if (now >= nextReportMs) {
            QJsonArray routeJson;
            for (Route &route : routes) {
                QJsonObject item;
                item.insert(QStringLiteral("slot"), route.slot);
                item.insert(
                    QStringLiteral("streamId"), QString::number(route.streamId)
                );
                if (!route.active) {
                    item.insert(QStringLiteral("state"), QStringLiteral("idle"));
                    routeJson.append(item);
                    continue;
                }
                const WebRtcProductDiagnostics diagnostics =
                    controller.diagnosticsSnapshot(route.streamId);
                const EndpointSnapshot senderSnapshot = route.sender->snapshot();
                const std::uint64_t presented = diagnostics.media.presentedFrames;
                const std::uint64_t delta = presented >= route.previousPresented
                                                ? presented - route.previousPresented
                                                : 0;
                if (now >= warmupMs + 1'000 && route.slot != 2 && delta == 0) {
                    result.continuityPassed = false;
                }
                route.previousPresented = presented;
                maximumTransportQueue = std::max(
                    maximumTransportQueue, senderSnapshot.queueDepth
                );
                maximumDecodeQueue = std::max(
                    maximumDecodeQueue, diagnostics.media.queuePackets
                );
                maximumDecodeBytes = std::max(
                    maximumDecodeBytes, diagnostics.media.queueBytes
                );
                item.insert(
                    QStringLiteral("state"),
                    QString::fromLatin1(WebRtcProductPolicy::stateName(
                        diagnostics.state
                    ))
                );
                item.insert(
                    QStringLiteral("endpointGeneration"),
                    QString::number(diagnostics.transport.generation)
                );
                item.insert(
                    QStringLiteral("mediaGeneration"),
                    QString::number(diagnostics.mediaGeneration)
                );
                item.insert(QStringLiteral("presentedDelta"),
                            static_cast<qint64>(delta));
                item.insert(QStringLiteral("presentedFrames"),
                            static_cast<qint64>(presented));
                item.insert(QStringLiteral("transportQueue"),
                            static_cast<qint64>(senderSnapshot.queueDepth));
                item.insert(QStringLiteral("transportDrops"),
                            static_cast<qint64>(
                                senderSnapshot.droppedAccessUnits +
                                diagnostics.transport.receiveDrops));
                item.insert(QStringLiteral("decodeQueuePackets"),
                            diagnostics.media.queuePackets);
                item.insert(QStringLiteral("decodeQueueBytes"),
                            diagnostics.media.queueBytes);
                item.insert(QStringLiteral("decodeDrops"),
                            static_cast<qint64>(diagnostics.media.packetsDropped));
                item.insert(QStringLiteral("mailboxOverwritten"),
                            static_cast<qint64>(
                                diagnostics.media.mailboxOverwrittenFrames));
                item.insert(QStringLiteral("internalLatencyP50Ms"),
                            diagnostics.media.internalLatencyP50Ms);
                item.insert(QStringLiteral("internalLatencyP95Ms"),
                            diagnostics.media.internalLatencyP95Ms);
                item.insert(QStringLiteral("internalLatencyMaxMs"),
                            diagnostics.media.internalLatencyMaxMs);
                item.insert(QStringLiteral("presentationIntervalP50Ms"),
                            diagnostics.media.presentationIntervalP50Ms);
                item.insert(QStringLiteral("presentationIntervalP95Ms"),
                            diagnostics.media.presentationIntervalP95Ms);
                item.insert(QStringLiteral("presentationIntervalMaxMs"),
                            diagnostics.media.presentationIntervalMaxMs);
                routeJson.append(item);
            }
            const RenderRuntimeMetrics renderer = window.rendererRuntimeMetrics();
            const QJsonObject sharedRenderer {
                {QStringLiteral("scope"), QStringLiteral("shared_renderer")},
                {QStringLiteral("uploadCpuUs"), renderer.uploadCpuUs},
                {QStringLiteral("paintCpuUs"), renderer.paintCpuUs},
                {QStringLiteral("textureBytes"), renderer.textureBytes}
            };
            if (rebuilt && directAtMs < 0 && routes[1].active &&
                controller.state(routes[1].streamId) == WebRtcProductState::Direct) {
                directAtMs = now;
            }
            if (rebuilt && directAtMs < 0 && now - rebuiltAtMs > 10'000) {
                result.recoveryWithinTenSeconds = false;
            }
            emitJson(QJsonObject {
                {QStringLiteral("type"), QStringLiteral("sample")},
                {QStringLiteral("scenario"), options.scenario},
                {QStringLiteral("phase"), phaseName(now, options.warmupSeconds)},
                {QStringLiteral("elapsedSeconds"), now / 1'000},
                {QStringLiteral("routes"), routeJson},
                {QStringLiteral("sharedRenderer"), sharedRenderer}
            });
            nextReportMs += 1'000;
        }
        QThread::msleep(2);
    }

    if (rebuilt && (directAtMs < 0 || directAtMs - rebuiltAtMs > 10'000)) {
        result.recoveryWithinTenSeconds = false;
    }
    result.queuesPassed = maximumTransportQueue <= 2U &&
                          maximumDecodeQueue <= 45 &&
                          maximumDecodeBytes <= 4 * 1024 * 1024;
    pacer.stop();
    for (Route &route : routes) closeRoute(&route, controller);
    controller.cancel();
    QElapsedTimer cleanup;
    cleanup.start();
    while ((manager.streamCount() != 0 || window.videoWidgetCount() != 0) &&
           cleanup.elapsed() < 2'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    result.cleanupPassed = manager.streamCount() == 0 &&
                           window.videoWidgetCount() == 0 &&
                           !controller.isActive();
    result.passed = result.reason == QStringLiteral("none") &&
                    result.fifthRejected && result.oldPortRejected &&
                    result.recoveryWithinTenSeconds &&
                    result.continuityPassed && result.queuesPassed &&
                    result.cleanupPassed;
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    QApplication application(argc, argv);
    const ParseResult parsed = parseOptions(application.arguments());
    if (!parsed.ok) {
        emitJson(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("reason"), parsed.reason}
        });
        return 2;
    }
    if (parsed.options.selfTest) {
        const bool passed = runSelfTest();
        emitJson(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("selfTest")},
            {QStringLiteral("passed"), passed}
        });
        return passed ? 0 : 1;
    }
    auto fixture = loadFixtureCycle(parsed.options.samplePath);
    if (!fixture.has_value()) {
        emitJson(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("reason"), QStringLiteral("fixture_unavailable")}
        });
        return 3;
    }
    const std::size_t fixtureCount = fixture->size();
    const ScenarioResult result = runScenario(
        parsed.options, std::move(*fixture)
    );
    const bool cleanupGlobal =
        WebRtcProductSessionController::cleanupGlobal();
    emitJson(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("result")},
        {QStringLiteral("scenario"), parsed.options.scenario},
        {QStringLiteral("fixtureAccessUnits"), static_cast<qint64>(fixtureCount)},
        {QStringLiteral("passed"), result.passed && cleanupGlobal},
        {QStringLiteral("officialDuration"), result.officialDuration},
        {QStringLiteral("sameMachineSoftwareQualified"),
         result.passed && cleanupGlobal && result.officialDuration},
        {QStringLiteral("fifthRejected"), result.fifthRejected},
        {QStringLiteral("oldPortRejected"), result.oldPortRejected},
        {QStringLiteral("recoveryWithinTenSeconds"),
         result.recoveryWithinTenSeconds},
        {QStringLiteral("continuityPassed"), result.continuityPassed},
        {QStringLiteral("queuesPassed"), result.queuesPassed},
        {QStringLiteral("cleanupPassed"), result.cleanupPassed && cleanupGlobal},
        {QStringLiteral("reason"), result.reason}
    });
    return result.passed && cleanupGlobal ? 0 : 1;
}
