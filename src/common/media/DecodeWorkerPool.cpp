#include "media/DecodeWorkerPool.h"

#include <algorithm>
#include <utility>

DecodeWorkerPool::DecodeWorkerPool(int workerCount)
{
    const int boundedCount = std::max(1, workerCount);
    workers_.reserve(static_cast<std::size_t>(boundedCount));

    for (int index = 0; index < boundedCount; ++index) {
        auto worker = std::make_unique<Worker>();
        Worker *workerPointer = worker.get();
        worker->thread.reset(QThread::create([workerPointer] {
            runWorker(workerPointer);
        }));
        worker->thread->setObjectName(
            QStringLiteral("DecodeWorker%1").arg(index + 1, 2, 10, QLatin1Char('0'))
        );
        worker->thread->start();
        workers_.push_back(std::move(worker));
    }
}

DecodeWorkerPool::~DecodeWorkerPool()
{
    stop();
}

int DecodeWorkerPool::workerCount() const noexcept
{
    return static_cast<int>(workers_.size());
}

int DecodeWorkerPool::workerIndexFor(std::uint64_t stableKey) const noexcept
{
    if (workers_.empty()) {
        return 0;
    }
    return static_cast<int>(stableKey % workers_.size());
}

bool DecodeWorkerPool::post(int workerIndex, std::function<void()> task)
{
    if (task == nullptr || workerIndex < 0 || workerIndex >= workerCount()) {
        return false;
    }

    Worker *worker = workers_.at(static_cast<std::size_t>(workerIndex)).get();
    {
        const std::lock_guard<std::mutex> lock(worker->mutex);
        if (worker->stopping) {
            return false;
        }
        worker->tasks.push_back(std::move(task));
    }
    worker->condition.notify_one();
    return true;
}

void DecodeWorkerPool::stop()
{
    if (stopped_) {
        return;
    }
    stopped_ = true;

    for (const auto &worker : workers_) {
        {
            const std::lock_guard<std::mutex> lock(worker->mutex);
            worker->stopping = true;
        }
        worker->condition.notify_all();
    }
    for (const auto &worker : workers_) {
        if (worker->thread != nullptr) {
            worker->thread->wait();
        }
    }
    workers_.clear();
}

void DecodeWorkerPool::runWorker(Worker *worker)
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(worker->mutex);
            worker->condition.wait(lock, [worker] {
                return worker->stopping || !worker->tasks.empty();
            });
            if (worker->stopping && worker->tasks.empty()) {
                break;
            }
            task = std::move(worker->tasks.front());
            worker->tasks.pop_front();
        }
        task();
    }
}
