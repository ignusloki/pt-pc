#include "engine/core/parallel_for.h"

#include <deque>
#include <vector>

namespace pt {

namespace {

class Pool {
public:
    Pool() {
        const size_t threads = std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 8);
        for (size_t i = 0; i < threads; ++i) {
            std::thread([this] { Run(); }).detach();
        }
    }

    void Submit(const std::shared_ptr<PoolWork>& work, size_t helpers) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(work);
        }
        if (helpers == 1) {
            wake_.notify_one();
        } else {
            wake_.notify_all();
        }
    }

    void Remove(const PoolWork* work) {
        std::lock_guard lock(mutex_);
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if (it->get() == work) {
                queue_.erase(it);
                return;
            }
        }
    }

private:
    void Run() {
        for (;;) {
            std::shared_ptr<PoolWork> work;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return !queue_.empty(); });
                work = queue_.front();
            }
            while (work->RunOne()) {
            }
            Remove(work.get());
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<PoolWork>> queue_;
};

Pool& ThePool() {
    static Pool* pool = new Pool();
    return *pool;
}

}

void PoolSubmit(const std::shared_ptr<PoolWork>& work, size_t helpers) {
    ThePool().Submit(work, helpers);
}

void PoolRemove(const PoolWork* work) {
    ThePool().Remove(work);
}

struct ParallelTask::State final : PoolWork {
    enum : int { kQueued, kRunning, kDone, kCancelled };
    std::function<void()> fn;
    std::atomic<int> stage{kQueued};
    std::mutex mutex;
    std::condition_variable finished;

    bool Claim() {
        int expected = kQueued;
        return stage.compare_exchange_strong(expected, kRunning);
    }
    void Execute() {
        fn();
        fn = nullptr;
        {
            std::lock_guard lock(mutex);
            stage = kDone;
        }
        finished.notify_all();
    }
    bool RunOne() override {
        if (Claim()) {
            Execute();
        }
        return false;
    }
    void WaitDone() {
        std::unique_lock lock(mutex);
        finished.wait(lock, [&] { return stage.load() == kDone; });
    }
};

ParallelTask::ParallelTask(std::function<void()> fn) : state_(std::make_shared<State>()) {
    state_->fn = std::move(fn);
    PoolSubmit(state_, 1);
}

ParallelTask::~ParallelTask() {
    Cancel();
}

bool ParallelTask::Done() const {
    const int stage = state_->stage.load();
    return stage == State::kDone || stage == State::kCancelled;
}

void ParallelTask::Wait() {
    if (state_->Claim()) {
        PoolRemove(state_.get());
        state_->Execute();
        return;
    }
    if (state_->stage.load() != State::kCancelled) {
        state_->WaitDone();
    }
}

bool ParallelTask::Cancel() {
    int expected = State::kQueued;
    if (state_->stage.compare_exchange_strong(expected, State::kCancelled)) {
        PoolRemove(state_.get());
        state_->fn = nullptr;
        return true;
    }
    if (expected != State::kCancelled) {
        state_->WaitDone();
    }
    return false;
}

}
