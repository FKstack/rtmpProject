#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

enum class ApplicationOptionsParseStatus {
    Ready,
    VersionDisplayed,
    Invalid,
};

/** @brief Validated command-line values independent of object composition. */
struct ApplicationOptions
{
    QStringList streamUrls;
    int decodeWorkerCount = 1;
    int maximumReconnectFailures = 0;
    QString metricsFile;
    QString rendererName = QStringLiteral("auto");
    QString displayFps = QStringLiteral("auto");
    bool latencyMarkerEnabled = false;
    QString logLevel;
    bool logLevelSet = false;
    QString logDirectory;
    bool logDirectorySet = false;
    QString logConfig;
    QString mediaServerConfig;
    bool mediaServerConfigSet = false;
    bool cameraAutostartDisabled = false;
    bool validationLayout = false;
    QString directConfig;
    QString directValidationScenario;
    QString directResult;

    [[nodiscard]] static ApplicationOptionsParseStatus parse(
        QCoreApplication &application,
        ApplicationOptions *options,
        QString *error
    );
};
