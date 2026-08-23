#pragma once

#include "media/MultiStreamPlaybackManager.h"
#include "webrtc_client/WebRtcClientRuntime.h"

#include <QObject>

#include <functional>
#include <memory>

class VideoCanvasHost;
class QWidget;

namespace rtmp_monitor::webrtc_client {

/** UI-thread owner of the single-stream media and canvas composition. */
class WebRtcViewerController final : public QObject
{
public:
    WebRtcViewerController(
        ClientEventSink eventSink,
        std::shared_ptr<WebRtcViewerEvidence> evidence,
        QObject *parent = nullptr
    );
    ~WebRtcViewerController() override;

    [[nodiscard]] rtmp_monitor::webrtc_transport::H264ReceiveSink
        receiveSink() const;
    void setCloseCallback(std::function<void()> callback);
    void show();
    void shutdown();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateSnapshot();
    void observeDecodedFrame();
    void observePresentedFrame();
    [[nodiscard]] bool framebufferContainsImage() const;
    void emitEvent(const QString &event, QJsonObject details = {}) const;

    ClientEventSink eventSink_;
    std::shared_ptr<WebRtcViewerEvidence> evidence_;
    MultiStreamPlaybackManager manager_;
    std::shared_ptr<EncodedVideoInputHandle> input_;
    std::shared_ptr<LatestFrameMailbox> mailbox_;
    StreamId streamId_ = kInvalidStreamId;
    QWidget *window_ = nullptr;
    VideoCanvasHost *canvas_ = nullptr;
    std::function<void()> closeCallback_;
    std::uint64_t snapshotGeneration_ = 0;
    bool closeNotified_ = false;
    bool decodedEmitted_ = false;
    bool presentedEmitted_ = false;
    bool presentationCheckPending_ = false;
    bool shutdown_ = false;
};

} // namespace rtmp_monitor::webrtc_client
