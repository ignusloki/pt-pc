#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "engine/render/vk.h"

namespace pt::vk {

constexpr size_t kMaxPipelineCacheBytes = 64u * 1024u * 1024u;

struct PipelineCacheIdentity {
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint32_t driver_version = 0;
    std::array<uint8_t, VK_UUID_SIZE> pipeline_cache_uuid{};
};

struct PipelineCreationStats {
    uint64_t graphics_count = 0;
    uint64_t compute_count = 0;
    uint64_t duration_ns = 0;
    uint64_t background_count = 0;
    uint64_t background_ns = 0;
};

PipelineCacheIdentity PipelineCacheIdentityFor(const VkPhysicalDeviceProperties& properties);
std::filesystem::path PipelineCacheFilePath(const std::filesystem::path& directory, const PipelineCacheIdentity& identity);
bool EncodePipelineCacheFile(const PipelineCacheIdentity& identity, std::span<const uint8_t> payload, std::vector<uint8_t>& file);
bool DecodePipelineCacheFile(const PipelineCacheIdentity& identity, std::span<const uint8_t> file, std::vector<uint8_t>& payload);
bool ReadPipelineCacheFile(const std::filesystem::path& path, const PipelineCacheIdentity& identity, std::vector<uint8_t>& payload,
                           std::string& status);
bool WritePipelineCacheFileAtomic(const std::filesystem::path& path, const PipelineCacheIdentity& identity,
                                  std::span<const uint8_t> payload, std::string& status);

struct PipelineCacheSavePolicy {
    std::chrono::steady_clock::duration settle = std::chrono::seconds(3);
    std::chrono::steady_clock::duration spacing = std::chrono::seconds(30);
    uint64_t saved_count = 0;
    uint64_t seen_count = 0;
    std::chrono::steady_clock::time_point seen_at{};
    std::chrono::steady_clock::time_point saved_at{};
    bool requested = false;
    bool Due(uint64_t count, std::chrono::steady_clock::time_point now);
    void Saved(uint64_t count, std::chrono::steady_clock::time_point now);
};

class PipelineCacheSaver {
public:
    ~PipelineCacheSaver();
    void Configure(VkDevice device, VkPipelineCache cache, std::filesystem::path path, PipelineCacheIdentity identity,
                   std::span<const uint8_t> loaded);
    void Poll();
    void Request();
    void Join();
    bool Configured() const { return device_ != VK_NULL_HANDLE && cache_ != VK_NULL_HANDLE && !path_.empty(); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
    std::filesystem::path path_;
    PipelineCacheIdentity identity_;
    PipelineCacheSavePolicy policy_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    uint64_t written_hash_ = 0;
};

void RegisterPipelineCache(VkDevice device, VkPipelineCache cache);
void UnregisterPipelineCache(VkDevice device);
PipelineCreationStats PipelineStats(VkDevice device);
std::mutex& PipelineCacheCreationMutex();
void MarkPipelineWarmupThread();
VkResult CreateGraphicsPipelinesCached(VkDevice device, uint32_t count, const VkGraphicsPipelineCreateInfo* infos,
                                      const VkAllocationCallbacks* allocator, VkPipeline* pipelines, const char* label = nullptr);
VkResult CreateComputePipelinesCached(VkDevice device, uint32_t count, const VkComputePipelineCreateInfo* infos,
                                     const VkAllocationCallbacks* allocator, VkPipeline* pipelines, const char* label = nullptr);

}
