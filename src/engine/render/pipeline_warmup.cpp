#include "engine/render/pipeline_warmup.h"

#include "engine/render/pipeline_cache_store.h"

#include <algorithm>
#include <chrono>

namespace pt::vk {
namespace {

int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

PipelineWarmup::~PipelineWarmup() { Join(); }

void PipelineWarmup::Add(std::function<uint32_t()> job) {
    if (!started_) jobs_.push_back(std::move(job));
}

void PipelineWarmup::Start(uint32_t threads) {
    if (started_) return;
    started_ = true;
    start_ns_ = NowNs();
    if (jobs_.empty()) {
        end_ns_ = start_ns_.load();
        return;
    }
    const uint32_t count = std::clamp<uint32_t>(threads, 1u, static_cast<uint32_t>(jobs_.size()));
    for (uint32_t i = 0; i < count; ++i) workers_.emplace_back(&PipelineWarmup::Work, this);
}

void PipelineWarmup::Work() {
    MarkPipelineWarmupThread();
    for (size_t i = next_++; i < jobs_.size(); i = next_++) {
        pipelines_ += jobs_[i]();
        if (++finished_ == jobs_.size()) end_ns_ = NowNs();
    }
}

double PipelineWarmup::Seconds() const {
    const int64_t stored = end_ns_.load();
    const int64_t end = Done() && stored ? stored : NowNs();
    return started_ ? static_cast<double>(end - start_ns_.load()) * 1e-9 : 0.0;
}

void PipelineWarmup::Join() {
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

}
