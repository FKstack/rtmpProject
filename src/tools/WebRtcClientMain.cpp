#include "webrtc_client/WebRtcClientOptions.h"
#include "webrtc_client/WebRtcClientRuntime.h"
#include "webrtc_client/WebRtcViewerController.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTextStream>

#include <rtc/rtc.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <mutex>

using namespace rtmp_monitor::webrtc_client;

namespace {

bool cleanupRtc(const ClientEventSink &eventSink)
{
    try {
        auto cleanup = rtc::Cleanup();
        if (cleanup.wait_for(std::chrono::seconds(10)) ==
            std::future_status::timeout) {
            eventSink(
                QStringLiteral("cleanup_failed"),
                QJsonObject {
                    {QStringLiteral("error"),
                     QStringLiteral("cleanup_timeout")}
                }
            );
            return false;
        }
        cleanup.get();
        return true;
    } catch (...) {
        eventSink(
            QStringLiteral("cleanup_failed"),
            QJsonObject {
                {QStringLiteral("error"), QStringLiteral("library_failure")}
            }
        );
        return false;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("rtmp_monitor_webrtc_client")
    );
    rtc::InitLogger(rtc::LogLevel::None);

    QCommandLineParser parser;
    WebRtcClientOptions::configureParser(parser);
    if (!parser.parse(QCoreApplication::arguments())) {
        QTextStream(stdout)
            << "{\"error\":\"invalid_arguments\","
               "\"event\":\"invalid_arguments\"}"
            << Qt::endl;
        return 2;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        QString helpText = parser.helpText();
        helpText.replace(
            QCoreApplication::arguments().constFirst(),
            QCoreApplication::applicationName()
        );
        QTextStream(stdout) << helpText;
        return 0;
    }
    const auto options = WebRtcClientOptions::fromParser(parser);
    if (!options.has_value()) {
        QTextStream(stdout)
            << "{\"error\":\"invalid_arguments\","
               "\"event\":\"invalid_arguments\"}"
            << Qt::endl;
        return 2;
    }

    const auto outputMutex = std::make_shared<std::mutex>();
    const QString mediaRole = mediaRoleName(options->mediaRole);
    const QString signalingRole = signalingRoleName(options->signalingRole);
    ClientEventSink eventSink = [
        outputMutex,
        mediaRole,
        signalingRole,
        applicationPointer = &application
    ](const QString &event, QJsonObject details) {
        details.insert(QStringLiteral("event"), event);
        if (!details.contains(QStringLiteral("mediaRole"))) {
            details.insert(QStringLiteral("mediaRole"), mediaRole);
        }
        if (!details.contains(QStringLiteral("role"))) {
            details.insert(QStringLiteral("role"), signalingRole);
        }
        {
            const std::lock_guard lock(*outputMutex);
            QTextStream(stdout)
                << QJsonDocument(details).toJson(QJsonDocument::Compact)
                << Qt::endl;
        }
        if (event == QStringLiteral("completed") ||
            event == QStringLiteral("failed")) {
            QMetaObject::invokeMethod(
                applicationPointer,
                &QCoreApplication::quit,
                Qt::QueuedConnection
            );
        }
    };

    std::shared_ptr<WebRtcViewerEvidence> viewerEvidence;
    std::unique_ptr<WebRtcViewerController> viewer;
    rtmp_monitor::webrtc_transport::H264ReceiveSink receiveSink;
    if (options->mediaRole == ClientMediaRole::Viewer) {
        viewerEvidence = std::make_shared<WebRtcViewerEvidence>();
        try {
            viewer = std::make_unique<WebRtcViewerController>(
                eventSink, viewerEvidence
            );
            receiveSink = viewer->receiveSink();
        } catch (...) {
            eventSink(
                QStringLiteral("failed"),
                QJsonObject {
                    {QStringLiteral("error"),
                     QStringLiteral("viewer_initialization_failed")}
                }
            );
            (void)cleanupRtc(eventSink);
            return 4;
        }
    } else {
        application.setQuitOnLastWindowClosed(false);
    }

    WebRtcClientRuntime runtime(
        *options,
        eventSink,
        std::move(receiveSink),
        viewerEvidence
    );
    if (viewer) {
        viewer->setCloseCallback([&runtime, &application] {
            runtime.requestStop();
            QMetaObject::invokeMethod(
                &application,
                &QCoreApplication::quit,
                Qt::QueuedConnection
            );
        });
        viewer->show();
    }
    if (!runtime.start()) {
        eventSink(
            QStringLiteral("failed"),
            QJsonObject {
                {QStringLiteral("error"), QStringLiteral("invalid_state")}
            }
        );
        if (viewer) viewer->shutdown();
        (void)cleanupRtc(eventSink);
        return 4;
    }

    (void)application.exec();
    runtime.requestStop();
    runtime.join();
    const int result = runtime.exitCode();
    if (viewer) viewer->shutdown();
    return cleanupRtc(eventSink) ? result : 4;
}
