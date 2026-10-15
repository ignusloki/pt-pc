#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace pt {

struct PoolWork {
    virtual ~PoolWork() = default;
    virtual bool RunOne() = 0;
};

void PoolSubmit(const std::shared_ptr<PoolWork>& work, size_t helpers);
void PoolRemove(const PoolWork* work);

namespace detail {

struct ChunkBatch final : PoolWork {
    void (*run)(void* context, size_t chunk) = nullptr;
    void* context = nullptr;
    size_t chunks = 0;
    std::atomic<size_t> next{0};
    std::atomic<size_t> done{0};
    std::mutex mutex;
    std::condition_variable finished;

    bool RunOne() override {
        const size_t chunk = next.fetch_add(1);
        if (chunk >= chunks) {
            return false;
        }
        run(context, chunk);
        if (done.fetch_add(1) + 1 == chunks) {
            std::lock_guard lock(mutex);
            finished.notify_all();
        }
        return true;
    }
};

}

template <typename Fn>
void ParallelChunks(size_t count, size_t chunks, Fn&& fn) {
    chunks = std::max<size_t>(1, std::min(chunks, count));
    if (chunks <= 1) {
        fn(size_t{0}, size_t{0}, count);
        return;
    }
    struct Context {
        Fn* fn;
        size_t count;
        size_t chunks;
    } context{&fn, count, chunks};
    auto batch = std::make_shared<detail::ChunkBatch>();
    batch->run = [](void* p, size_t c) {
        const Context& ctx = *static_cast<const Context*>(p);
        (*ctx.fn)(c, ctx.count * c / ctx.chunks, ctx.count * (c + 1) / ctx.chunks);
    };
    batch->context = &context;
    batch->chunks = chunks;
    PoolSubmit(batch, chunks - 1);
    while (batch->RunOne()) {
    }
    {
        std::unique_lock lock(batch->mutex);
        batch->finished.wait(lock, [&] { return batch->done.load() == chunks; });
    }
    PoolRemove(batch.get());
}

inline size_t ParallelChunkCount(size_t count, size_t min_per_chunk) {
    const size_t cores = std::max<size_t>(1, std::thread::hardware_concurrency());
    return std::max<size_t>(1, std::min<size_t>({cores, size_t{8}, count / std::max<size_t>(1, min_per_chunk)}));
}

class ParallelTask {
public:
    explicit ParallelTask(std::function<void()> fn);
    ~ParallelTask();
    ParallelTask(const ParallelTask&) = delete;
    ParallelTask& operator=(const ParallelTask&) = delete;

    bool Done() const;
    void Wait();
    bool Cancel();

private:
    struct State;
    std::shared_ptr<State> state_;
};

}
