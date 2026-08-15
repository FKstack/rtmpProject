#include <QtTest>

#include "render/EmbeddedGlCapabilities.h"

namespace {

EmbeddedGlCapabilities makeQualifiedEs3()
{
    EmbeddedGlCapabilities capabilities;
    capabilities.qpaPlatform = QStringLiteral("eglfs");
    capabilities.isOpenGles = true;
    capabilities.actualMajor = 3;
    capabilities.actualMinor = 1;
    capabilities.vendor = QStringLiteral("vendor");
    capabilities.renderer = QStringLiteral("renderer");
    capabilities.version = QStringLiteral("OpenGL ES 3.1");
    capabilities.maxTextureSize = 8192;
    capabilities.maxCombinedTextureUnits = 8;
    capabilities.supportsRequiredRedRgTextures = true;
    capabilities.supportsRequiredUnpackRowLength = true;
    capabilities.framebufferComplete = true;
    capabilities.shaderSmokePassed = true;
    return capabilities;
}

} // namespace

class EmbeddedGlCapabilitiesTest final : public QObject
{
    Q_OBJECT

private slots:
    void qualifiedEs3ContextPasses()
    {
        const EmbeddedGlQualification result =
            qualifyEmbeddedGlCapabilities(makeQualifiedEs3());
        QVERIFY2(result.qualified, qPrintable(result.reason));
        QVERIFY(result.reason.isEmpty());
    }

    void qualifiedDesktopContextPasses()
    {
        EmbeddedGlCapabilities capabilities = makeQualifiedEs3();
        capabilities.qpaPlatform = QStringLiteral("xcb");
        capabilities.isOpenGles = false;
        capabilities.actualMajor = 3;
        capabilities.actualMinor = 3;
        const EmbeddedGlQualification result =
            qualifyEmbeddedGlCapabilities(capabilities);
        QVERIFY2(result.qualified, qPrintable(result.reason));
    }

    void linuxfbAlwaysFallsBackToCpu()
    {
        EmbeddedGlCapabilities capabilities = makeQualifiedEs3();
        capabilities.qpaPlatform = QStringLiteral("linuxfb");
        const EmbeddedGlQualification result =
            qualifyEmbeddedGlCapabilities(capabilities);
        QVERIFY(!result.qualified);
        QVERIFY(result.reason.contains(QStringLiteral("linuxfb")));
    }

    void esVersionBelow30Fails()
    {
        EmbeddedGlCapabilities capabilities = makeQualifiedEs3();
        capabilities.actualMajor = 2;
        capabilities.actualMinor = 0;
        const EmbeddedGlQualification result =
            qualifyEmbeddedGlCapabilities(capabilities);
        QVERIFY(!result.qualified);
        QVERIFY(result.reason.contains(QStringLiteral("3.0")));
    }

    void desktopVersionBelow33Fails()
    {
        EmbeddedGlCapabilities capabilities = makeQualifiedEs3();
        capabilities.isOpenGles = false;
        capabilities.actualMajor = 3;
        capabilities.actualMinor = 2;
        QVERIFY(!qualifyEmbeddedGlCapabilities(capabilities).qualified);
    }

    void insufficientTextureSizeFails()
    {
        EmbeddedGlCapabilities capabilities = makeQualifiedEs3();
        capabilities.maxTextureSize = 1024;
        QVERIFY(!qualifyEmbeddedGlCapabilities(capabilities).qualified);
    }

    void insufficientTextureUnitsFails()
    {
        EmbeddedGlCapabilities capabilities = makeQualifiedEs3();
        capabilities.maxCombinedTextureUnits = 2;
        QVERIFY(!qualifyEmbeddedGlCapabilities(capabilities).qualified);
    }

    void eachRequiredFactHasItsOwnReason()
    {
        EmbeddedGlCapabilities capabilities = makeQualifiedEs3();

        capabilities.supportsRequiredRedRgTextures = false;
        QVERIFY(!qualifyEmbeddedGlCapabilities(capabilities).qualified);
        capabilities = makeQualifiedEs3();

        capabilities.supportsRequiredUnpackRowLength = false;
        QVERIFY(!qualifyEmbeddedGlCapabilities(capabilities).qualified);
        capabilities = makeQualifiedEs3();

        capabilities.shaderSmokePassed = false;
        QVERIFY(!qualifyEmbeddedGlCapabilities(capabilities).qualified);
        capabilities = makeQualifiedEs3();

        capabilities.framebufferComplete = false;
        QVERIFY(!qualifyEmbeddedGlCapabilities(capabilities).qualified);
    }
};

QTEST_GUILESS_MAIN(EmbeddedGlCapabilitiesTest)

#include "EmbeddedGlCapabilitiesTest.moc"
