#include "ui/CpuVideoCanvas.h"

#include <QElapsedTimer>
#include <QPainter>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <unordered_set>
#include <utility>

#include "ui/VideoCanvasHost.h"

namespace {

qint64 monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           )
        .count();
}

} // namespace

CpuVideoCanvas::CpuVideoCanvas(VideoCanvasHost *host, QWidget *parent)
    : QWidget(parent)
    , host_(host)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
}

void CpuVideoCanvas::paintEvent(QPaintEvent *)
{
    QElapsedTimer timer;
    timer.start();
    (void)host_->controller_->consumeDirty();

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    qint64 latestAge = -1;
    std::unordered_set<StreamId> activeStreams;
    for (const RenderItem &item : host_->controller_->snapshot().items) {
        if (item.streamId != kInvalidStreamId) {
            activeStreams.insert(item.streamId);
        }
        if (!item.frameVisible || item.streamId == kInvalidStreamId) {
            continue;
        }
        auto &cache = caches_[item.streamId];
        if (const auto frame = host_->controller_->consumeFrame(
                item.streamId, cache.sequence);
            frame.has_value()) {
            QImage image = cache.converter.convert(*frame);
            if (!image.isNull()) {
                cache.image = std::move(image);
                cache.sequence = frame->sequence();
                ++host_->statistics_.uploadedFrames;
                if (const auto mailbox = host_->controller_->mailbox(
                        item.streamId);
                    mailbox != nullptr) {
                    mailbox->recordUploaded();
                }
                latestAge = std::max<qint64>(
                    0, monotonicMilliseconds() - frame->receivedMonotonicMs()
                );
            } else {
                ++host_->statistics_.unsupportedFrames;
            }
        }
        if (cache.image.isNull()) {
            continue;
        }
        const VideoPlacement placement = calculateVideoPlacement(
            item.videoViewport,
            cache.image.size(),
            item.displayMode
        );
        const QRectF source(
            placement.sourceUv.x() * cache.image.width(),
            placement.sourceUv.y() * cache.image.height(),
            placement.sourceUv.width() * cache.image.width(),
            placement.sourceUv.height() * cache.image.height()
        );
        painter.drawImage(placement.targetRect, cache.image, source);
        if (const auto mailbox = host_->controller_->mailbox(item.streamId);
            mailbox != nullptr) {
            mailbox->recordRendered();
        }
        ++host_->statistics_.renderedFrames;
    }
    for (auto iterator = caches_.begin(); iterator != caches_.end();) {
        iterator = activeStreams.find(iterator->first) == activeStreams.end()
                       ? caches_.erase(iterator)
                       : std::next(iterator);
    }
    ++host_->statistics_.paintCalls;
    host_->statistics_.lastPaintCpuUs = timer.nsecsElapsed() / 1000;
    host_->statistics_.lastUploadCpuUs = 0;
    host_->statistics_.lastGpuTimeUs = -1;
    host_->statistics_.latestFrameAgeMs = latestAge;
    host_->statistics_.textureBytes = 0;
    host_->onSurfacePainted();
}
