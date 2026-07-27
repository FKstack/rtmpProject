#pragma once

#include <QThread>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

/**
 * @brief 为状态型视频解码器提供固定归属 worker 的轻量任务池。
 *
 * 网络读取不进入该池。调用方用稳定 key 选择 worker，同一路的 AVCodecContext
 * 因而始终在同一个线程串行使用。
 */
class DecodeWorkerPool final
{
public:
    explicit DecodeWorkerPool(int workerCount);
    ~DecodeWorkerPool();

    DecodeWorkerPool(const DecodeWorkerPool &) = delete;
    DecodeWorkerPool &operator=(const DecodeWorkerPool &) = delete;

    [[nodiscard]] int workerCount() const noexcept;
    [[nodiscard]] int workerIndexFor(std::uint64_t stableKey) const noexcept;

    bool post(int workerIndex, std::function<void()> task);
    void stop();

private:
    struct Worker
    {
        std::unique_ptr<QThread> thread;
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<std::function<void()>> tasks;
        bool stopping = false;
    };

    static void runWorker(Worker *worker);

    std::vector<std::unique_ptr<Worker>> workers_;
    bool stopped_ = false;
};
