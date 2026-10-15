#include "engine/render/pipeline_cache_store.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <span>
#include <unordered_map>

#include "engine/core/log.h"
#include "engine/render/pipeline_warmup.h"

namespace pt::vk {
namespace {

std::mutex g_pipeline_mutex;
struct PipelineDeviceState {
    VkPipelineCache cache = VK_NULL_HANDLE;
    PipelineCreationStats stats;
};
std::unordered_map<VkDevice, PipelineDeviceState> g_pipeline_devices;
const bool g_pipeline_log = std::getenv("PT_PIPELINE_LOG") != nullptr;
std::atomic<bool> g_play_phase{false};
thread_local bool t_warmup_thread = false;

VkPipelineCache CacheFor(VkDevice device) {
    const auto it = g_pipeline_devices.find(device);
    return it == g_pipeline_devices.end() ? VK_NULL_HANDLE : it->second.cache;
}

}

void RegisterPipelineCache(VkDevice device, VkPipelineCache cache) {
    std::lock_guard lock(g_pipeline_mutex);
    g_pipeline_devices[device].cache = cache;
}

void UnregisterPipelineCache(VkDevice device) {
    std::lock_guard lock(g_pipeline_mutex);
    g_pipeline_devices.erase(device);
}

PipelineCreationStats PipelineStats(VkDevice device) {
    std::lock_guard lock(g_pipeline_mutex);
    const auto it = g_pipeline_devices.find(device);
    return it == g_pipeline_devices.end() ? PipelineCreationStats{} : it->second.stats;
}

std::mutex& PipelineCacheCreationMutex() { return g_pipeline_mutex; }

void MarkPipelineWarmupThread() { t_warmup_thread = true; }

void SetPipelinePlayPhase(bool play) { g_play_phase = play; }

namespace {

template <typename Create>
VkResult CreateTimed(VkDevice device, uint32_t count, bool compute, const char* label, Create&& create) {
    VkPipelineCache cache = VK_NULL_HANDLE;
    {
        std::lock_guard lock(g_pipeline_mutex);
        cache = CacheFor(device);
    }
    const auto start = std::chrono::steady_clock::now();
    const VkResult result = create(cache);
    const uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    {
        std::lock_guard lock(g_pipeline_mutex);
        if (auto it = g_pipeline_devices.find(device); it != g_pipeline_devices.end()) {
            (compute ? it->second.stats.compute_count : it->second.stats.graphics_count) += count;
            it->second.stats.duration_ns += ns;
            if (t_warmup_thread) {
                it->second.stats.background_count += count;
                it->second.stats.background_ns += ns;
            }
        }
    }
    const double ms = static_cast<double>(ns) * 1e-6;
    if (g_pipeline_log) {
        LogInfo("pipeline: {} {} {:.2f} ms{}", compute ? "compute" : "graphics", label ? label : "?", ms, t_warmup_thread ? " (warm-up)" : "");
    } else if (g_play_phase && !t_warmup_thread) {
        LogInfo("pipeline: {} made while playing in {:.1f} ms", label ? label : "?", ms);
    }
    return result;
}

}

VkResult CreateGraphicsPipelinesCached(VkDevice device, uint32_t count, const VkGraphicsPipelineCreateInfo* infos,
                                      const VkAllocationCallbacks* allocator, VkPipeline* pipelines, const char* label) {
    return CreateTimed(device, count, false, label, [&](VkPipelineCache cache) {
        return vkCreateGraphicsPipelines(device, cache, count, infos, allocator, pipelines);
    });
}

VkResult CreateComputePipelinesCached(VkDevice device, uint32_t count, const VkComputePipelineCreateInfo* infos,
                                     const VkAllocationCallbacks* allocator, VkPipeline* pipelines, const char* label) {
    return CreateTimed(device, count, true, label, [&](VkPipelineCache cache) {
        return vkCreateComputePipelines(device, cache, count, infos, allocator, pipelines);
    });
}

namespace {

uint64_t PayloadHash(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash ^ bytes.size();
}

}

PipelineCacheSaver::~PipelineCacheSaver() { Join(); }

void PipelineCacheSaver::Configure(VkDevice device, VkPipelineCache cache, std::filesystem::path path, PipelineCacheIdentity identity,
                                   std::span<const uint8_t> loaded) {
    Join();
    device_ = device;
    cache_ = cache;
    path_ = std::move(path);
    identity_ = identity;
    policy_ = {};
    written_hash_ = loaded.empty() ? 0 : PayloadHash(loaded);
}

void PipelineCacheSaver::Request() { policy_.requested = true; }

void PipelineCacheSaver::Join() {
    if (worker_.joinable()) worker_.join();
}

void PipelineCacheSaver::Poll() {
    if (!Configured() || busy_) return;
    const PipelineCreationStats stats = PipelineStats(device_);
    const uint64_t count = stats.graphics_count + stats.compute_count;
    const auto now = std::chrono::steady_clock::now();
    if (!policy_.Due(count, now)) return;
    policy_.Saved(count, now);
    Join();
    busy_ = true;
    worker_ = std::thread([this, count] {
        const auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> data;
        size_t size = 0;
        VkResult result = vkGetPipelineCacheData(device_, cache_, &size, nullptr);
        if (result == VK_SUCCESS && size <= kMaxPipelineCacheBytes) {
            data.resize(size);
            result = vkGetPipelineCacheData(device_, cache_, &size, data.data());
            data.resize(size);
        }
        if (result != VK_SUCCESS || size > kMaxPipelineCacheBytes) {
            LogWarn("vulkan: pipeline cache not saved while running ({}, {} bytes)", vk::ResultName(result), size);
        } else if (const uint64_t hash = PayloadHash(data); hash != written_hash_) {
            std::string status;
            if (WritePipelineCacheFileAtomic(path_, identity_, data, status)) {
                written_hash_ = hash;
                LogInfo("vulkan: pipeline cache saved {} bytes while running ({} pipelines made, {:.0f} ms on a worker)", data.size(), count,
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            } else {
                LogWarn("vulkan: pipeline cache not saved while running ({})", status);
            }
        }
        busy_ = false;
    });
}

}
