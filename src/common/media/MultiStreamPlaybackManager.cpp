#include "media/MultiStreamPlaybackManager.h"

#include <QThread>

MultiStreamPlaybackManager::MultiStreamPlaybackManager(
    const QStringList &streamUrls,
    QObject *parent
)
    : QObject(parent)
    , streamUrls_(streamUrls)
{
    players_.reserve(static_cast<std::size_t>(streamUrls_.size()));

    for (int streamIndex = 0; streamIndex < streamUrls_.size(); ++streamIndex) {
        auto player = std::make_unique<FFmpegPlayer>();
        player->setObjectName(
            QStringLiteral("streamPlayer%1").arg(streamIndex + 1, 2, 10, QLatin1Char('0'))
        );

        connect(
            player.get(), &FFmpegPlayer::frameReady,
            this,
            [this, streamIndex](const QImage &image) {
                emit frameReady(streamIndex, image);
            }
        );
        connect(
            player.get(), &FFmpegPlayer::stateChanged,
            this,
            [this, streamIndex](FFmpegPlayer::PlaybackState state) {
                emit stateChanged(streamIndex, state);
            }
        );
        connect(
            player.get(), &FFmpegPlayer::errorOccurred,
            this,
            [this, streamIndex](const QString &message) {
                emit errorOccurred(streamIndex, message);
            }
        );

        players_.push_back(std::move(player));
    }
}

MultiStreamPlaybackManager::~MultiStreamPlaybackManager()
{
    stopAll();
}

int MultiStreamPlaybackManager::streamCount() const noexcept
{
    return static_cast<int>(players_.size());
}

bool MultiStreamPlaybackManager::startStream(int streamIndex)
{
    Q_ASSERT(QThread::currentThread() == thread());

    FFmpegPlayer *player = playerAt(streamIndex);
    return player != nullptr && player->start(streamUrls_.at(streamIndex));
}

void MultiStreamPlaybackManager::stopStream(int streamIndex)
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (FFmpegPlayer *player = playerAt(streamIndex); player != nullptr) {
        player->stop();
    }
}

int MultiStreamPlaybackManager::startAll()
{
    Q_ASSERT(QThread::currentThread() == thread());

    int startedCount = 0;
    for (int streamIndex = 0; streamIndex < streamCount(); ++streamIndex) {
        if (startStream(streamIndex)) {
            ++startedCount;
        }
    }
    return startedCount;
}

void MultiStreamPlaybackManager::stopAll()
{
    Q_ASSERT(QThread::currentThread() == thread());

    // 先发布全部取消请求，使四路阻塞 I/O 和重连等待并行开始退出。
    for (const auto &player : players_) {
        player->requestStop();
    }
    for (const auto &player : players_) {
        player->stop();
    }
}

bool MultiStreamPlaybackManager::isStreamRunning(int streamIndex) const noexcept
{
    Q_ASSERT(QThread::currentThread() == thread());

    const FFmpegPlayer *player = playerAt(streamIndex);
    return player != nullptr && player->isRunning();
}

FFmpegPlayer *MultiStreamPlaybackManager::playerAt(int streamIndex) const noexcept
{
    if (streamIndex < 0 || streamIndex >= streamCount()) {
        return nullptr;
    }
    return players_.at(static_cast<std::size_t>(streamIndex)).get();
}
