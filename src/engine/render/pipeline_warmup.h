#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pt::vk {

class PipelineWarmup {
public:
    ~PipelineWarmup();
    void Add(std::function<uint32_t()> job);
    void Start(uint32_t threads);
    bool Started() const { return started_; }
    bool Done() const { return finished_.load() == jobs_.size(); }
    uint32_t Finished() const { return static_cast<uint32_t>(finished_.load()); }
    uint32_t Total() const { return static_cast<uint32_t>(jobs_.size()); }
    uint32_t Pipelines() const { return pipelines_.load(); }
    double Seconds() const;
    void Join();

private:
    void Work();
    std::vector<std::function<uint32_t()>> jobs_;
    std::vector<std::thread> workers_;
    std::atomic<size_t> next_{0};
    std::atomic<size_t> finished_{0};
    std::atomic<uint32_t> pipelines_{0};
    std::atomic<int64_t> start_ns_{0};
    std::atomic<int64_t> end_ns_{0};
    bool started_ = false;
};

void SetPipelinePlayPhase(bool play);

}
