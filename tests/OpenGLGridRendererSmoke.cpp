#include <QtTest>

#include <QDir>
#include <QOpenGLWidget>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "media/VideoFrame.h"
#include "media/VideoFrameConverter.h"
#include "ui/VideoCanvasHost.h"

namespace {

struct QualityMetrics
{
    double meanAbsoluteError = 0.0;
    double psnr = 0.0;
    int p99AbsoluteError = 0;
    QImage difference;
};

VideoFrame solidFrame(
    VideoPixelFormat format,
    VideoMatrixCoefficients matrix,
    VideoColorRange range,
    std::uint8_t yValue,
    std::uint8_t uValue,
    std::uint8_t vValue
)
{
    constexpr int width = 64;
    constexpr int height = 48;
    constexpr int chromaWidth = width / 2;
    constexpr int chromaHeight = height / 2;
    std::vector<std::uint8_t> y(width * height, yValue);
    std::vector<std::uint8_t> u(chromaWidth * chromaHeight, uValue);
    std::vector<std::uint8_t> v(chromaWidth * chromaHeight, vValue);
    std::vector<std::uint8_t> uv(chromaWidth * chromaHeight * 2);
    for (int index = 0; index < chromaWidth * chromaHeight; ++index) {
        uv[2 * index] = uValue;
        uv[2 * index + 1] = vValue;
    }
    std::array<VideoPlaneView, VideoFrame::kMaximumPlanes> planes {{
        {y.data(), width, width, height},
        format == VideoPixelFormat::Yuv420P8
            ? VideoPlaneView {u.data(), chromaWidth, chromaWidth, chromaHeight}
            : VideoPlaneView {uv.data(), chromaWidth * 2, chromaWidth * 2,
                              chromaHeight},
        {v.data(), chromaWidth, chromaWidth, chromaHeight},
    }};
    const auto frame = VideoFrame::copyFromPlanes(
        width,
        height,
        format,
        planes,
        0,
        1,
        {1, 30},
        {
            matrix == VideoMatrixCoefficients::Bt2020Ncl
                ? VideoColorPrimaries::Bt2020
                : (matrix == VideoMatrixCoefficients::Bt709
                       ? VideoColorPrimaries::Bt709
                       : VideoColorPrimaries::Bt601_625),
            matrix == VideoMatrixCoefficients::Bt2020Ncl
                ? VideoTransferFunction::Bt2020_10
                : VideoTransferFunction::Bt709,
            matrix,
            range,
        },
        1,
        1,
        0
    );
    Q_ASSERT(frame.has_value());
    return *frame;
}

VideoFrame verticalBandsFrame()
{
    constexpr int width = 64;
    constexpr int height = 48;
    constexpr int chromaWidth = width / 2;
    constexpr int chromaHeight = height / 2;
    std::vector<std::uint8_t> y(width * height, 140);
    for (int row = 0; row < height; ++row) {
        const std::uint8_t value = row < height / 3
                                       ? std::uint8_t(50)
                                       : (row >= 2 * height / 3
                                              ? std::uint8_t(220)
                                              : std::uint8_t(140));
        std::fill_n(y.begin() + row * width, width, value);
    }
    std::vector<std::uint8_t> u(chromaWidth * chromaHeight, 128);
    std::vector<std::uint8_t> v(chromaWidth * chromaHeight, 128);
    std::array<VideoPlaneView, VideoFrame::kMaximumPlanes> planes {{
        {y.data(), width, width, height},
        {u.data(), chromaWidth, chromaWidth, chromaHeight},
        {v.data(), chromaWidth, chromaWidth, chromaHeight},
    }};
    const auto frame = VideoFrame::copyFromPlanes(
        width,
        height,
        VideoPixelFormat::Yuv420P8,
        planes,
        0,
        1,
        {1, 30},
        {
            VideoColorPrimaries::Bt709,
            VideoTransferFunction::Bt709,
            VideoMatrixCoefficients::Bt709,
            VideoColorRange::Limited,
        },
        1,
        1,
        0
    );
    Q_ASSERT(frame.has_value());
    return *frame;
}

VideoFrame edgeMarkerFrame(int width, int height)
{
    const int border = std::max(2, std::min(width, height) / 10);
    const int chromaWidth = width / 2;
    const int chromaHeight = height / 2;
    std::vector<std::uint8_t> y(width * height, 120);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            std::uint8_t value = 120;
            if (row < border) {
                value = 40;
            } else if (row >= height - border) {
                value = 80;
            } else if (column < border) {
                value = 170;
            } else if (column >= width - border) {
                value = 220;
            }
            y[row * width + column] = value;
        }
    }
    std::vector<std::uint8_t> u(chromaWidth * chromaHeight, 128);
    std::vector<std::uint8_t> v(chromaWidth * chromaHeight, 128);
    std::array<VideoPlaneView, VideoFrame::kMaximumPlanes> planes {{
        {y.data(), width, width, height},
        {u.data(), chromaWidth, chromaWidth, chromaHeight},
        {v.data(), chromaWidth, chromaWidth, chromaHeight},
    }};
    const auto frame = VideoFrame::copyFromPlanes(
        width,
        height,
        VideoPixelFormat::Yuv420P8,
        planes,
        0,
        1,
        {1, 30},
        {
            VideoColorPrimaries::Bt709,
            VideoTransferFunction::Bt709,
            VideoMatrixCoefficients::Bt709,
            VideoColorRange::Limited,
        },
        1,
        1,
        0
    );
    Q_ASSERT(frame.has_value());
    return *frame;
}

QualityMetrics compareImages(const QImage &referenceInput, const QImage &actualInput)
{
    const QImage reference = referenceInput.convertToFormat(QImage::Format_RGB888);
    const QImage actual = actualInput.convertToFormat(QImage::Format_RGB888);
    Q_ASSERT(reference.size() == actual.size());
    QualityMetrics metrics;
    metrics.difference = QImage(reference.size(), QImage::Format_RGB888);
    std::vector<int> errors;
    errors.reserve(static_cast<std::size_t>(reference.width()) *
                   reference.height() * 3);
    double absoluteErrorSum = 0.0;
    double squaredErrorSum = 0.0;
    for (int y = 0; y < reference.height(); ++y) {
        const auto *referenceRow = reference.constScanLine(y);
        const auto *actualRow = actual.constScanLine(y);
        auto *differenceRow = metrics.difference.scanLine(y);
        for (int x = 0; x < reference.width() * 3; ++x) {
            const int error = std::abs(
                static_cast<int>(referenceRow[x]) -
                static_cast<int>(actualRow[x])
            );
            errors.push_back(error);
            absoluteErrorSum += error;
            squaredErrorSum += static_cast<double>(error * error);
            differenceRow[x] = static_cast<std::uint8_t>(
                std::min(255, error * 8)
            );
        }
    }
    const double count = static_cast<double>(errors.size());
    metrics.meanAbsoluteError = absoluteErrorSum / count;
    const double meanSquaredError = squaredErrorSum / count;
    metrics.psnr = meanSquaredError == 0.0
                       ? std::numeric_limits<double>::infinity()
                       : 10.0 * std::log10(255.0 * 255.0 / meanSquaredError);
    std::sort(errors.begin(), errors.end());
    const std::size_t p99Index = std::min(
        errors.size() - 1,
        static_cast<std::size_t>(std::ceil(errors.size() * 0.99)) - 1
    );
    metrics.p99AbsoluteError = errors[p99Index];
    return metrics;
}

QString caseName(VideoPixelFormat format, VideoMatrixCoefficients matrix,
                 VideoColorRange range)
{
    const QString formatName = format == VideoPixelFormat::Yuv420P8
                                   ? QStringLiteral("yuv420p")
                                   : QStringLiteral("nv12");
    QString matrixName = QStringLiteral("bt601");
    if (matrix == VideoMatrixCoefficients::Bt709) {
        matrixName = QStringLiteral("bt709");
    } else if (matrix == VideoMatrixCoefficients::Bt2020Ncl) {
        matrixName = QStringLiteral("bt2020ncl");
    }
    return QStringLiteral("%1-%2-%3")
        .arg(formatName, matrixName,
             range == VideoColorRange::Full ? QStringLiteral("full")
                                            : QStringLiteral("limited"));
}

} // namespace

class OpenGLGridRendererSmoke final : public QObject
{
    Q_OBJECT

private slots:
    void rendererMatchesCpuReference_data();
    void rendererMatchesCpuReference();
    void displayModesFillWideViewport_data();
    void displayModesFillWideViewport();
    void containShowsEverySourceEdge_data();
    void containShowsEverySourceEdge();
};

void OpenGLGridRendererSmoke::rendererMatchesCpuReference_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<int>("matrix");
    QTest::addColumn<int>("range");
    QTest::addColumn<int>("y");
    QTest::addColumn<int>("u");
    QTest::addColumn<int>("v");
    const std::array<VideoPixelFormat, 2> formats {
        VideoPixelFormat::Yuv420P8, VideoPixelFormat::Nv12_8
    };
    for (VideoPixelFormat format : formats) {
        for (const auto &entry : {
                 std::array<int, 5> {
                     static_cast<int>(VideoMatrixCoefficients::Bt601),
                     static_cast<int>(VideoColorRange::Limited), 81, 90, 240
                 },
                 std::array<int, 5> {
                     static_cast<int>(VideoMatrixCoefficients::Bt709),
                     static_cast<int>(VideoColorRange::Limited), 63, 102, 240
                 },
                 std::array<int, 5> {
                     static_cast<int>(VideoMatrixCoefficients::Bt2020Ncl),
                     static_cast<int>(VideoColorRange::Limited), 70, 100, 220
                 },
                 std::array<int, 5> {
                     static_cast<int>(VideoMatrixCoefficients::Bt709),
                     static_cast<int>(VideoColorRange::Full), 76, 85, 255
                 }}) {
            const auto matrix = static_cast<VideoMatrixCoefficients>(entry[0]);
            const auto range = static_cast<VideoColorRange>(entry[1]);
            const QByteArray rowName = caseName(format, matrix, range).toLatin1();
            QTest::newRow(rowName.constData())
                << static_cast<int>(format) << entry[0] << entry[1]
                << entry[2] << entry[3] << entry[4];
        }
    }
}

void OpenGLGridRendererSmoke::rendererMatchesCpuReference()
{
    QFETCH(int, format);
    QFETCH(int, matrix);
    QFETCH(int, range);
    QFETCH(int, y);
    QFETCH(int, u);
    QFETCH(int, v);
    const VideoFrame frame = solidFrame(
        static_cast<VideoPixelFormat>(format),
        static_cast<VideoMatrixCoefficients>(matrix),
        static_cast<VideoColorRange>(range),
        static_cast<std::uint8_t>(y),
        static_cast<std::uint8_t>(u),
        static_cast<std::uint8_t>(v)
    );
    VideoFrameToImageConverter converter;
    const QImage cpuReference = converter.convert(frame);
    QVERIFY(!cpuReference.isNull());

    VideoCanvasHost host(RendererPreference::OpenGL);
    host.resize(frame.width(), frame.height());
    auto mailbox = std::make_shared<LatestFrameMailbox>();
    host.registerStream(1, mailbox);
    const QRectF logicalViewport(host.rect());
    RenderSnapshot snapshot;
    snapshot.logicalCanvasSize = host.size();
    snapshot.devicePixelRatio = host.devicePixelRatioF();
    snapshot.items.push_back({
        1, logicalViewport, logicalViewport, VideoDisplayMode::Contain,
        QStringLiteral("synthetic"), {}, true, false, false,
    });
    host.setSnapshot(std::move(snapshot));
    host.show();
    QVERIFY(mailbox->submit(frame));

    QTRY_COMPARE_WITH_TIMEOUT(
        host.activeBackendName(), QStringLiteral("opengl"), 5'000
    );
    QTRY_VERIFY_WITH_TIMEOUT(host.statistics().uploadedFrames >= 1, 5'000);
    const RenderRuntimeMetrics runtime = host.runtimeMetrics();
    QCOMPARE(runtime.activeBackend, QStringLiteral("opengl"));
    QVERIFY(!runtime.openGlVendor.isEmpty());
    QVERIFY(!runtime.openGlRenderer.isEmpty());
    QVERIFY(!runtime.openGlVersion.isEmpty());

    QImage framebuffer = host.grabFramebufferImage();
    QVERIFY(!framebuffer.isNull());
    QImage scaledReference(framebuffer.size(), QImage::Format_RGB888);
    scaledReference.fill(Qt::black);
    {
        QPainter painter(&scaledReference);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const VideoPlacement placement = calculateVideoPlacement(
            logicalViewport, cpuReference.size(), VideoDisplayMode::Contain
        );
        const qreal dpr = host.devicePixelRatioF();
        painter.drawImage(
            QRectF(
                placement.targetRect.x() * dpr,
                placement.targetRect.y() * dpr,
                placement.targetRect.width() * dpr,
                placement.targetRect.height() * dpr
            ),
            cpuReference
        );
    }
    const QualityMetrics quality = compareImages(scaledReference, framebuffer);
    const QString qualityCase = caseName(
        static_cast<VideoPixelFormat>(format),
        static_cast<VideoMatrixCoefficients>(matrix),
        static_cast<VideoColorRange>(range)
    );
    qInfo().noquote()
        << QStringLiteral("QUALITY case=%1 psnr=%2 mae=%3 p99=%4")
               .arg(qualityCase)
               .arg(quality.psnr, 0, 'f', 4)
               .arg(quality.meanAbsoluteError, 0, 'f', 4)
               .arg(quality.p99AbsoluteError);
    const QString artifactRoot = qEnvironmentVariable(
        "RTMP_MONITOR_TEST_ARTIFACT_DIR"
    );
    if (!artifactRoot.isEmpty()) {
        QDir directory(artifactRoot);
        QVERIFY(directory.mkpath(QStringLiteral(".")));
        const QString prefix = qualityCase;
        QVERIFY(scaledReference.save(directory.filePath(prefix + "-cpu.png")));
        QVERIFY(framebuffer.save(directory.filePath(prefix + "-opengl.png")));
        QVERIFY(quality.difference.save(directory.filePath(prefix + "-diff-x8.png")));
    }
    QVERIFY2(quality.psnr >= 35.0,
             qPrintable(QStringLiteral("PSNR=%1").arg(quality.psnr)));
    QVERIFY2(quality.meanAbsoluteError <= 3.0,
             qPrintable(QStringLiteral("MAE=%1").arg(quality.meanAbsoluteError)));
    QVERIFY2(quality.p99AbsoluteError <= 8,
             qPrintable(QStringLiteral("P99=%1").arg(quality.p99AbsoluteError)));
}

void OpenGLGridRendererSmoke::displayModesFillWideViewport_data()
{
    QTest::addColumn<int>("preference");
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("edgesFilled");
    QTest::newRow("cpu-cover")
        << static_cast<int>(RendererPreference::Cpu)
        << static_cast<int>(VideoDisplayMode::Cover) << true;
    QTest::newRow("cpu-contain")
        << static_cast<int>(RendererPreference::Cpu)
        << static_cast<int>(VideoDisplayMode::Contain) << false;
    QTest::newRow("opengl-cover")
        << static_cast<int>(RendererPreference::OpenGL)
        << static_cast<int>(VideoDisplayMode::Cover) << true;
    QTest::newRow("opengl-contain")
        << static_cast<int>(RendererPreference::OpenGL)
        << static_cast<int>(VideoDisplayMode::Contain) << false;
}

void OpenGLGridRendererSmoke::displayModesFillWideViewport()
{
    QFETCH(int, preference);
    QFETCH(int, mode);
    QFETCH(bool, edgesFilled);
    const RendererPreference rendererPreference =
        static_cast<RendererPreference>(preference);
    const VideoDisplayMode displayMode = static_cast<VideoDisplayMode>(mode);
    const VideoFrame frame = verticalBandsFrame();

    VideoCanvasHost host(rendererPreference);
    host.resize(400, 100);
    auto mailbox = std::make_shared<LatestFrameMailbox>();
    host.registerStream(1, mailbox);
    RenderSnapshot snapshot;
    snapshot.logicalCanvasSize = host.size();
    snapshot.devicePixelRatio = host.devicePixelRatioF();
    snapshot.items.push_back({
        1, QRectF(host.rect()), QRectF(host.rect()), displayMode,
        QStringLiteral("wide-synthetic"), {}, true, false, false,
    });
    host.setSnapshot(std::move(snapshot));
    host.show();
    QVERIFY(mailbox->submit(frame));

    QTRY_VERIFY_WITH_TIMEOUT(host.statistics().uploadedFrames >= 1, 5'000);
    if (rendererPreference == RendererPreference::OpenGL) {
        QCOMPARE(host.activeBackendName(), QStringLiteral("opengl"));
    } else {
        QCOMPARE(host.activeBackendName(), QStringLiteral("cpu"));
    }
    const QImage framebuffer = host.grabFramebufferImage();
    QVERIFY(!framebuffer.isNull());
    const int middleY = framebuffer.height() / 2;
    const int leftLuma = qGray(framebuffer.pixel(2, middleY));
    const int rightLuma = qGray(framebuffer.pixel(framebuffer.width() - 3, middleY));
    if (edgesFilled) {
        QVERIFY(leftLuma > 80);
        QVERIFY(rightLuma > 80);
        const int topLuma = qGray(framebuffer.pixel(
            framebuffer.width() / 2, framebuffer.height() / 10
        ));
        const int bottomLuma = qGray(framebuffer.pixel(
            framebuffer.width() / 2, framebuffer.height() * 9 / 10
        ));
        QVERIFY(topLuma > 90 && topLuma < 190);
        QVERIFY(bottomLuma > 90 && bottomLuma < 190);
    } else {
        QVERIFY(leftLuma < 10);
        QVERIFY(rightLuma < 10);
    }
}

void OpenGLGridRendererSmoke::containShowsEverySourceEdge_data()
{
    QTest::addColumn<int>("preference");
    QTest::addColumn<QSize>("sourceSize");
    const std::array<std::pair<const char *, QSize>, 3> cases {{
        {"16x9", QSize(160, 90)},
        {"4x3", QSize(160, 120)},
        {"portrait", QSize(96, 160)},
    }};
    for (const auto &[name, sourceSize] : cases) {
        QTest::newRow(QByteArray("cpu-").append(name).constData())
            << static_cast<int>(RendererPreference::Cpu) << sourceSize;
        QTest::newRow(QByteArray("opengl-").append(name).constData())
            << static_cast<int>(RendererPreference::OpenGL) << sourceSize;
    }
}

void OpenGLGridRendererSmoke::containShowsEverySourceEdge()
{
    QFETCH(int, preference);
    QFETCH(QSize, sourceSize);
    const RendererPreference rendererPreference =
        static_cast<RendererPreference>(preference);
    VideoCanvasHost host(rendererPreference);
    host.resize(320, 180);
    auto mailbox = std::make_shared<LatestFrameMailbox>();
    host.registerStream(1, mailbox);
    RenderSnapshot snapshot;
    snapshot.logicalCanvasSize = host.size();
    snapshot.devicePixelRatio = host.devicePixelRatioF();
    snapshot.items.push_back({
        1, QRectF(host.rect()), QRectF(host.rect()), VideoDisplayMode::Contain,
        QStringLiteral("edge-markers"), {}, true, false, false,
    });
    host.setSnapshot(std::move(snapshot));
    host.show();
    QVERIFY(mailbox->submit(edgeMarkerFrame(
        sourceSize.width(), sourceSize.height()
    )));
    QTRY_VERIFY_WITH_TIMEOUT(host.statistics().uploadedFrames >= 1, 5'000);
    QCOMPARE(
        host.activeBackendName(),
        rendererPreference == RendererPreference::OpenGL
            ? QStringLiteral("opengl")
            : QStringLiteral("cpu")
    );

    const QImage framebuffer = host.grabFramebufferImage();
    QVERIFY(!framebuffer.isNull());
    const VideoPlacement placement = calculateVideoPlacement(
        QRectF(host.rect()), sourceSize, VideoDisplayMode::Contain
    );
    const int x = qRound(placement.targetRect.center().x());
    const int y = qRound(placement.targetRect.center().y());
    const int sourceBorder = std::max(
        2, std::min(sourceSize.width(), sourceSize.height()) / 10
    );
    // Sample well inside each synthetic border. Sampling at 5% landed exactly
    // on the border boundary for a 160x90 source and could not detect a
    // vertically flipped OpenGL image.
    const int insetX = std::max(1, qRound(
        placement.targetRect.width() * sourceBorder * 0.4 /
        sourceSize.width()
    ));
    const int insetY = std::max(1, qRound(
        placement.targetRect.height() * sourceBorder * 0.4 /
        sourceSize.height()
    ));
    const int top = qGray(framebuffer.pixel(
        x, qRound(placement.targetRect.top()) + insetY
    ));
    const int bottom = qGray(framebuffer.pixel(
        x, qRound(placement.targetRect.bottom()) - insetY
    ));
    const int center = qGray(framebuffer.pixel(x, y));
    const int left = qGray(framebuffer.pixel(
        qRound(placement.targetRect.left()) + insetX, y
    ));
    const int right = qGray(framebuffer.pixel(
        qRound(placement.targetRect.right()) - insetX, y
    ));
    QVERIFY(top < bottom);
    QVERIFY(bottom < center);
    QVERIFY(center < left);
    QVERIFY(left < right);
    QVERIFY(top > 0);
    QVERIFY(right < 255);
}

QTEST_MAIN(OpenGLGridRendererSmoke)

#include "OpenGLGridRendererSmoke.moc"
