#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <atomic>
#include <future>
#include <mutex>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/assets/ftex.h"
#include "engine/core/parallel_for.h"
#include "engine/render/gpu/gpu_device.h"
#include "engine/render/gpu/gpu_vk_native.h"
#include "engine/render/vk_context.h"

namespace pt {
class QarArchive;
}

namespace pt {

struct TextureMip {
    uint32_t width = 0;
    uint32_t height = 0;
    std::span<const uint8_t> data;
};

struct MaterialGpu {
    uint32_t albedo = 0;
    uint32_t normal = 0;
    uint32_t specular = 0;
    uint32_t flags = 0;
    uint32_t aux0 = 0;
    uint32_t aux1 = 0;
    uint32_t kind = 0;
    uint32_t aux2 = 0;
    glm::vec4 albedo_factor{1.0f};
    glm::vec4 params{0.0f};
    glm::vec4 indices{0.0f};
    glm::vec4 extra{0.0f};
};

class TextureManager {
public:
    static constexpr uint32_t kMaxTextures = 8192;
    static constexpr uint32_t kMaxMaterials = 16384;
    static constexpr uint32_t kWhite = 0;
    static constexpr uint32_t kFlatNormal = 1;
    static constexpr uint32_t kBlack = 2;
    static constexpr uint32_t kClear = 6;
    static constexpr uint32_t kGrey = 4;
    static constexpr uint32_t kGreySrgb = 5;
    static constexpr uint32_t kMaxCubeTextures = 64;
    static constexpr uint32_t kNoCube = 0xFFFFFFFFu;

    bool Init(gpu::Device& device);
    void Shutdown();

    uint32_t Create(const std::string& name, gpu::Format format, std::span<const TextureMip> mips, uint32_t layers = 1, bool cube = false);
    uint32_t Find(const std::string& name) const;
    class ModelScope {
    public:
        explicit ModelScope(TextureManager& textures) : textures_(textures) { ++textures_.model_scope_; }
        ~ModelScope() { --textures_.model_scope_; }
        ModelScope(const ModelScope&) = delete;
        ModelScope& operator=(const ModelScope&) = delete;

    private:
        TextureManager& textures_;
    };
    void AddModelRef(uint32_t index);
    uint64_t ReleaseModelRef(uint32_t index);
    uint32_t FreeSlots() const { return static_cast<uint32_t>(free_.size()); }
    uint32_t LoadFox(const QarArchive& qar, const std::string& path, bool* ok = nullptr, bool raw = false);
    uint32_t LoadImageFile(const std::filesystem::path& path, const std::string& key);
    void DecodeAhead(const QarArchive& qar, const std::vector<std::string>& paths);
    bool StillDecoding(const std::string& path) const;
    uint32_t PumpDecoded(const QarArchive& qar, uint32_t count);
    bool AdoptDecoded(const std::string& path, std::shared_ptr<FtexTexture> decoded);
    void DropDecoded(const std::vector<std::string>& paths);
    std::unordered_set<std::string> Names() const;
    double ReadMs() const { return read_ms_; }
    double UploadMs() const { return upload_ms_; }
    uint32_t AddMaterial(const MaterialGpu& material);
    void UpdateMaterial(uint32_t index, const MaterialGpu& material);
    void RedirectTexture(uint32_t texture, uint32_t shown);
    void FlushMaterials();
    uint32_t MaterialFlags(uint32_t index) const { return index < materials_.size() ? materials_[index].flags : 0u; }
    int SetAnisotropy(int level);
    int Anisotropy() const { return anisotropy_; }
    void ConfigureEnhancedTextures(const QarArchive& qar, const std::filesystem::path& cache, uint64_t model, uint32_t kinds);
    void SetEnhancedTextures(bool enabled);
    bool EnhancedTextures() const { return enhanced_enabled_; }
    void PumpEnhancedTextures(double budget_ms);
    bool EnhancedPending() const { return enhanced_queue_ != nullptr; }
    void RefreshEnhancedTextures();
    std::vector<std::string> LoadedTextureStems() const;
    bool ReleaseEnhancedTextures(const char* why);
    bool EnhancedSuspended() const { return enhanced_suspended_; }

    gpu::BindingLayoutHandle BindingLayout() const { return set_layout_; }
    gpu::BindingSetHandle BindingSet() const { return set_; }
    uint32_t TextureCount() const { return static_cast<uint32_t>(images_.size()); }
    uint32_t MaterialCount() const { return static_cast<uint32_t>(materials_.size()); }
    uint32_t CubeSlot(uint32_t texture) const;

private:
    gpu::SamplerHandle CreateSampler(int anisotropy) const;
    void UpdateTextureDescriptor(uint32_t index);
    void Pin(uint32_t index) const;
    uint64_t FreeTexture(uint32_t index);
    void LoadEnhancedTexture(uint32_t index, const std::string& path, const FtexTexture* source = nullptr);
    uint32_t LoadModImage(const QarArchive& qar, const std::string& key, const std::string& stem, const std::vector<uint8_t>& png, bool raw);

    gpu::Device* device_ = nullptr;
    gpu::SamplerHandle base_sampler_;
    gpu::SamplerHandle sampler_;
    int anisotropy_ = 0;
    gpu::BindingLayoutHandle set_layout_;
    gpu::BindingPoolHandle pool_;
    gpu::BindingSetHandle set_;
    std::vector<gpu::ImageResource> images_;
    std::vector<std::string> names_;
    std::vector<uint32_t> model_refs_;
    mutable std::vector<uint8_t> pinned_;
    std::vector<uint32_t> free_;
    int model_scope_ = 0;
    std::vector<uint8_t> cube_;
    std::unordered_map<std::string, uint32_t> by_name_;
    std::unordered_map<uint32_t, uint32_t> cube_slots_;
    std::vector<MaterialGpu> materials_;
    std::unordered_map<uint32_t, uint32_t> redirects_;
    gpu::BufferResource material_buffer_;
    bool materials_dirty_ = false;
    const QarArchive* enhanced_qar_ = nullptr;
    std::filesystem::path enhanced_cache_;
    uint64_t enhanced_model_ = 0;
    uint32_t enhanced_kinds_ = 0;
    bool enhanced_enabled_ = false;
    bool enhanced_suspended_ = false;
    bool memory_short_ = false;
    std::unordered_map<uint32_t, std::string> fox_sources_;
    std::unordered_map<uint32_t, uint32_t> enhanced_images_;
    uint64_t enhanced_bytes_ = 0;
    struct EnhancedPrepared {
        uint32_t index = 0;
        std::string path;
        uint64_t key = 0;
        FtexTexture cached;
    };
    struct EnhancedQueue {
        std::mutex mutex;
        std::deque<EnhancedPrepared> ready;
        std::atomic<bool> stop{false};
        std::atomic<size_t> done{0};
        size_t total = 0;
        size_t applied = 0;
        bool refresh = false;
    };
    std::shared_ptr<EnhancedQueue> enhanced_queue_;
    std::future<void> enhanced_worker_;
    void StopEnhancedWorker();
    void StartEnhancedWorker(bool only_cached);
    void SetEnhancedTexturesFrom(bool enabled, bool only_cached);
    std::unordered_map<std::string, std::shared_future<std::shared_ptr<FtexTexture>>> decoding_;
    std::deque<std::string> decode_order_;
    std::vector<std::shared_ptr<ParallelTask>> decode_workers_;
    std::unordered_map<std::string, std::shared_ptr<ParallelTask>> decode_jobs_;
    double read_ms_ = 0.0;
    double upload_ms_ = 0.0;
};

uint32_t FormatBlockBytes(gpu::Format format, bool& compressed);

}
