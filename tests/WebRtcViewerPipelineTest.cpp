#include "media/MultiStreamPlaybackManager.h"
#include "ui/VideoCanvasHost.h"
#include "webrtc_transport/WebRtcEndpointSession.h"

#include <QByteArray>
#include <QImage>
#include <QSignalSpy>
#include <QtTest>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>

using namespace rtmp_monitor::webrtc_transport;

namespace {

H264AccessUnit fixedRedIdr(std::int64_t timestampUs = 0)
{
    const QByteArray bytes = QByteArray::fromBase64(
        "AAAAAWdCwB/cQmwEQAAAAwBAAAADAIPGDOAAAAABaM4PLIAAAAEGBf//WdxF6b3m2Ui3lizYINkj7u94MjY0IC0gY29yZSAxNjUgcjMyMjMgMDQ4MGNiMCAtIEguMjY0L01QRUctNCBBVkMgY29kZWMgLSBDb3B5bGVmdCAyMDAzLTIwMjUgLSBodHRwOi8vd3d3LnZpZGVvbGFuLm9yZy94MjY0Lmh0bWwgLSBvcHRpb25zOiBjYWJhYz0wIHJlZj0xIGRlYmxvY2s9MTowOjAgYW5hbHlzZT0weDE6MHgxMTEgbWU9aGV4IHN1Ym1lPTcgcHN5PTEgcHN5X3JkPTEuMDA6MC4wMiBtaXhlZF9yZWY9MCBtZV9yYW5nZT0xNiBjaHJvbWFfbWU9MSB0cmVsbGlzPTEgOHg4ZGN0PTAgY3FtPTAgZGVhZHpvbmU9MjEsMTEgZmFzdF9wc2tpcD0xIGNocm9tYV9xcF9vZmZzZXQ9LTIgdGhyZWFkcz0yIGxvb2thaGVhZF90aHJlYWRzPTEgc2xpY2VkX3RocmVhZHM9MCBucj0wIGRlY2ltYXRlPTEgaW50ZXJsYWNlZD0wIGJsdXJheV9jb21wYXQ9MCBjb25zdHJhaW5lZF9pbnRyYT0wIGJmcmFtZXM9MCB3ZWlnaHRwPTAga2V5aW50PTEga2V5aW50X21pbj0xIHNjZW5lY3V0PTAgaW50cmFfcmVmcmVzaD0wIHJjPWNyZiBtYnRyZWU9MCBjcmY9MjMuMCBxY29tcD0wLjYwIHFwbWluPTAgcXBtYXg9NjkgcXBzdGVwPTQgaXBfcmF0aW89MS40MCBhcT0xOjEuMDAAgAAAAWWIhAS8RigACovHAAEo2OAAL60nJyddddddddddddeA"
    );
    H264AccessUnit accessUnit;
    accessUnit.annexB.assign(bytes.cbegin(), bytes.cend());
    accessUnit.mediaTimestampUs = timestampUs;
    accessUnit.keyFrame = true;
    return accessUnit;
}

bool containsNonBlackPixel(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    if (image.isNull()) return false;
    const int stepX = std::max(1, image.width() / 32);
    const int stepY = std::max(1, image.height() / 18);
    for (int y = 0; y < image.height(); y += stepY) {
        for (int x = 0; x < image.width(); x += stepX) {
            const QRgb pixel = image.pixel(x, y);
            if (qRed(pixel) > 8 || qGreen(pixel) > 8 || qBlue(pixel) > 8) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

class WebRtcViewerPipelineTest final : public QObject
{
    Q_OBJECT

private slots:
    void realAuReachesDecoderMailboxAndCpuCanvas()
    {
        MultiStreamPlaybackManager manager;
        EncodedVideoInputHandle handle =
            manager.createEncodedVideoInput(QStringLiteral("Week 5 viewer"));
        QVERIFY(handle.isOpen());
        const StreamId streamId = handle.streamId();
        const auto mailbox = manager.frameMailbox(streamId);
        QVERIFY(mailbox != nullptr);
        const auto sharedHandle =
            std::make_shared<EncodedVideoInputHandle>(std::move(handle));

        VideoCanvasHost canvas(RendererPreference::Cpu);
        canvas.resize(640, 360);
        canvas.registerStream(streamId, mailbox);
        RenderSnapshot renderSnapshot;
        renderSnapshot.generation = 1;
        renderSnapshot.logicalCanvasSize = canvas.size();
        RenderItem item;
        item.streamId = streamId;
        item.tileRect = QRectF(canvas.rect());
        item.videoViewport = item.tileRect;
        item.frameVisible = true;
        renderSnapshot.items.push_back(item);
        canvas.setSnapshot(std::move(renderSnapshot));
        QSignalSpy presented(&canvas, &VideoCanvasHost::surfacePresented);
        canvas.show();

        WebRtcSessionConfig senderConfig;
        senderConfig.signalingRole = SignalingRole::Offerer;
        senderConfig.videoDirection = VideoDirection::SendOnly;
        WebRtcSessionConfig receiverConfig;
        receiverConfig.signalingRole = SignalingRole::Answerer;
        receiverConfig.videoDirection = VideoDirection::ReceiveOnly;
        WebRtcEndpointSession sender(senderConfig);
        WebRtcEndpointSession receiver(receiverConfig);
        std::atomic_bool dropFirst {true};
        const std::weak_ptr<EncodedVideoInputHandle> weakHandle(sharedHandle);
        QCOMPARE(
            receiver.setReceiveSink(
                [weakHandle, &dropFirst](SessionMediaSample sample) {
                    if (dropFirst.exchange(false)) {
                        return H264SubmitResult::DroppedCapacity;
                    }
                    const auto input = weakHandle.lock();
                    return input
                               ? input->submit(std::move(sample.accessUnit))
                               : H264SubmitResult::Closed;
                }
            ),
            EndpointError::None
        );

        const EndpointDescriptionResult offer = sender.createOffer();
        QVERIFY2(offer.ok(), WebRtcEndpointSession::errorName(offer.error));
        const EndpointDescriptionResult answer =
            receiver.acceptOfferAndCreateAnswer(offer.sdp);
        QVERIFY2(answer.ok(), WebRtcEndpointSession::errorName(answer.error));
        const EndpointConnectionResult senderConnected =
            sender.acceptAnswerAndWait(answer.sdp);
        const EndpointConnectionResult receiverConnected =
            receiver.waitConnected();
        QVERIFY(senderConnected.ok());
        QVERIFY(receiverConnected.ok());

        auto port = sender.createSendPort();
        QVERIFY(port.has_value());
        QCOMPARE(
            (*port)(fixedRedIdr()),
            H264SubmitResult::Accepted
        );
        QTRY_VERIFY_WITH_TIMEOUT(receiver.snapshot().receiveDrops >= 1, 5'000);
        QCOMPARE(mailbox->latestSequence(), std::uint64_t(0));

        QCOMPARE(
            (*port)(fixedRedIdr(33'333)),
            H264SubmitResult::Accepted
        );
        QTRY_VERIFY_WITH_TIMEOUT(
            receiver.snapshot().submittedAccessUnits >= 1,
            5'000
        );
        QTRY_VERIFY_WITH_TIMEOUT(mailbox->latestSequence() > 0, 5'000);
        const auto decoded = mailbox->latestAfter(0);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->width(), 64);
        QCOMPARE(decoded->height(), 64);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.statistics().renderedFrames > 0, 5'000);
        QVERIFY(presented.count() > 0);
        QVERIFY(mailbox->stats().rendered > 0);
        QVERIFY(containsNonBlackPixel(canvas.grabFramebufferImage()));

        sender.beginClose();
        sender.close();
        sender.close();
        receiver.close();
        receiver.close();
        sharedHandle->close();
        canvas.unregisterStream(streamId);
        QVERIFY(manager.removeStream(streamId));
        QCOMPARE(sender.snapshot().queueDepth, std::size_t(0));
    }

    void cleanupTestCase()
    {
        auto cleanup = rtc::Cleanup();
        QVERIFY(cleanup.wait_for(std::chrono::seconds(10)) !=
                std::future_status::timeout);
        cleanup.get();
    }
};

QTEST_MAIN(WebRtcViewerPipelineTest)
#include "WebRtcViewerPipelineTest.moc"
