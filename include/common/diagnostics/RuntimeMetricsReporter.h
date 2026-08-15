#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

#include "render/RenderRuntimeMetrics.h"

class MultiStreamPlaybackManager;
class QTimer;

/**
 * @brief Aggregates media and render observations into metrics schema v4.
 *
 * The reporter runs on the UI thread, owns the sampling/watchdog timers and
 * performs atomic JSON persistence. It intentionally sits above media and
 * render so neither subsystem depends on the other for diagnostics.
 */
class RuntimeMetricsReporter final : public QObject
{
    Q_OBJECT

public:
    explicit RuntimeMetricsReporter(
        MultiStreamPlaybackManager *playbackManager,
        QObject *parent = nullptr
    );
    ~RuntimeMetricsReporter() override;

    void setOutputPath(const QString &path);
    [[nodiscard]] QString outputPath() const;
    void setRenderMetricsProvider(
        std::function<RenderRuntimeMetrics()> provider
    );

    /** @brief Writes one snapshot immediately; returns false when disabled/failing. */
    bool writeSnapshot();
    [[nodiscard]] qint64 maximumUiTimerGapMs() const noexcept;

private:
    void observeUiTimer();

    MultiStreamPlaybackManager *playbackManager_ = nullptr;
    std::unique_ptr<QTimer> metricsTimer_;
    std::unique_ptr<QTimer> uiWatchdogTimer_;
    QString outputPath_;
    std::function<RenderRuntimeMetrics()> renderMetricsProvider_;
    qint64 lastUiWatchdogTickMs_ = 0;
    qint64 maximumUiTimerGapMs_ = 0;
};
