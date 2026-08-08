#include <QtTest>

#include "linux/LinuxRenderingPolicy.h"
#include "ui/FullscreenPresentationMode.h"

class LinuxRenderingPolicyTest final : public QObject
{
    Q_OBJECT

private slots:
    void explicitCpuAlwaysSelectsCpu()
    {
        const LinuxRenderingDecision decision = LinuxRenderingPolicy::decide(
            true, QStringLiteral("cpu"), QStringLiteral("eglfs")
        );
        QCOMPARE(decision.backend, LinuxRendererBackendChoice::Cpu);
        QVERIFY(!decision.fallbackOccurred);
    }

    void rasterBuildHonorsAutoWithCpu()
    {
        const LinuxRenderingDecision decision = LinuxRenderingPolicy::decide(
            false, QStringLiteral("auto"), QStringLiteral("eglfs")
        );
        QCOMPARE(decision.backend, LinuxRendererBackendChoice::Cpu);
        QVERIFY(!decision.fallbackOccurred);
        QVERIFY(!decision.reason.isEmpty());
    }

    void rasterBuildReportsExplicitOpenGlAsFallback()
    {
        const LinuxRenderingDecision decision = LinuxRenderingPolicy::decide(
            false, QStringLiteral("opengl"), QStringLiteral("xcb")
        );
        QCOMPARE(decision.backend, LinuxRendererBackendChoice::Cpu);
        QVERIFY(decision.fallbackOccurred);
        QVERIFY(!decision.reason.isEmpty());
    }

    void linuxfbNeverAttemptsGl()
    {
        const LinuxRenderingDecision autoDecision = LinuxRenderingPolicy::decide(
            true, QStringLiteral("auto"), QStringLiteral("linuxfb")
        );
        QCOMPARE(autoDecision.backend, LinuxRendererBackendChoice::Cpu);
        QVERIFY(!autoDecision.fallbackOccurred);
        QVERIFY(!autoDecision.reason.isEmpty());

        const LinuxRenderingDecision glDecision = LinuxRenderingPolicy::decide(
            true, QStringLiteral("opengl"), QStringLiteral("linuxfb")
        );
        QCOMPARE(glDecision.backend, LinuxRendererBackendChoice::Cpu);
        QVERIFY(glDecision.fallbackOccurred);
    }

    void eglfsMarksSingleTopLevelWindowConstraint()
    {
        const LinuxRenderingDecision decision = LinuxRenderingPolicy::decide(
            true, QStringLiteral("auto"), QStringLiteral("eglfs")
        );
        QCOMPARE(decision.backend, LinuxRendererBackendChoice::OpenGlEs3);
        QVERIFY(decision.singleGlTopLevelWindow);

        const LinuxRenderingDecision xcbDecision = LinuxRenderingPolicy::decide(
            true, QStringLiteral("auto"), QStringLiteral("xcb")
        );
        QCOMPARE(xcbDecision.backend, LinuxRendererBackendChoice::OpenGlEs3);
        QVERIFY(!xcbDecision.singleGlTopLevelWindow);
    }

    void explicitOpenGlOnGlBuildAttemptsEs3()
    {
        const LinuxRenderingDecision decision = LinuxRenderingPolicy::decide(
            true, QStringLiteral("opengl"), QStringLiteral("wayland")
        );
        QCOMPARE(decision.backend, LinuxRendererBackendChoice::OpenGlEs3);
        QVERIFY(!decision.fallbackOccurred);
    }

    void fullscreenPresentationModeFollowsQpa()
    {
        QCOMPARE(
            fullscreenPresentationModeForQpa(QStringLiteral("eglfs")),
            FullscreenPresentationMode::ReuseMainCanvas
        );
        QCOMPARE(
            fullscreenPresentationModeForQpa(QStringLiteral("EGLFS")),
            FullscreenPresentationMode::ReuseMainCanvas
        );
        QCOMPARE(
            fullscreenPresentationModeForQpa(QStringLiteral("xcb")),
            FullscreenPresentationMode::TemporaryWindowCanvas
        );
        QCOMPARE(
            fullscreenPresentationModeForQpa(QStringLiteral("wayland")),
            FullscreenPresentationMode::TemporaryWindowCanvas
        );
        QCOMPARE(
            fullscreenPresentationModeForQpa(QStringLiteral("linuxfb")),
            FullscreenPresentationMode::TemporaryWindowCanvas
        );
        QCOMPARE(
            fullscreenPresentationModeForQpa(QStringLiteral("windows")),
            FullscreenPresentationMode::TemporaryWindowCanvas
        );
    }
};

QTEST_GUILESS_MAIN(LinuxRenderingPolicyTest)

#include "LinuxRenderingPolicyTest.moc"
