#pragma once

#include "engine/render/vk_context.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace pt {

struct Camera;
struct RenderStats;
struct SceneLighting;
class Renderer;
namespace vfx { struct RenderList; }

// Read small patches of the lighting passes and finished scRGB images using the existing frame fences.
class LampTrace {
public:
    explicit LampTrace(std::filesystem::path directory);
    void Scene(uint64_t tick, const std::string& floor, const Camera& camera, const SceneLighting& lights,
               const RenderStats& stats, const vfx::RenderList& effects, float aspect, bool lens, const Renderer& renderer,
               SDL_Window* window, bool menu, bool paused);
    void Capture(vk::Context& ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkExtent2D extent,
                 uint32_t slot, bool interpolated, bool repeat, VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    enum Stage { Diffuse, SceneHdr, Reactive, Resolved, Bloom, Flare, BeforeComposite, StageCount };
    void CaptureStages(Renderer& renderer, const std::array<gpu::RenderTarget*, StageCount>& targets);
    void Mark(const char* reason);
    void FinishSlot(vk::Context& ctx, uint32_t slot);
    void Queued(const vk::Context& ctx, uint32_t id, uint32_t image, const char* kind, double submitted, VkResult result);
    void Displayed(const vk::Context& ctx, const VkPastPresentationTimingGOOGLE& timing);
    void Shutdown(vk::Context& ctx);

private:
    static constexpr unsigned kRegions = 15, kSide = 48, kProbes = 3;
    struct Region {
        std::string name;
        glm::vec2 uv;
        float depth = 0, alpha = 0;
        uint64_t light_id = 0;
        float light_intensity = 0;
        glm::vec2 source_uv{};
        int probe = -1;
    };
    struct Sample {
        std::array<Region, kRegions> regions;
        unsigned count = 0;
        std::string metadata;
        std::string lens_info;
        float paper = 0;
        float aspect = 1;
        uint64_t sequence = 0;
    } scene_;
    struct Readback {
        vk::Buffer buffer;
        Sample sample;
        VkExtent2D extent{};
        std::array<VkBufferImageCopy, kRegions> copies{};
        unsigned side = 0;
        const char* kind = nullptr;
    };
    std::array<std::array<Readback, 2>, 2> slots_;
    void Write(vk::Context& ctx, Readback& readback);
    struct StagePatch {
        unsigned stage, probe, region, side;
        VkOffset3D offset;
        VkExtent2D extent;
        bool unorm;
    };
    struct StageReadback {
        vk::Buffer buffer;
        std::array<StagePatch, StageCount * kProbes> patches;
        unsigned count = 0;
        Sample sample;
    };
    std::array<StageReadback, 2> stage_slots_;
    void WriteStages(vk::Context& ctx, StageReadback& r);
    void OpenSegment(uint64_t sequence);
    struct PreviousPatch {
        std::array<float, kSide * kSide> values{};
        VkOffset3D offset{};
        unsigned side = 0;
        uint64_t sequence = 0;
    };
    std::unordered_map<std::string, PreviousPatch> previous_;
    std::array<std::array<PreviousPatch, kProbes>, StageCount> previous_stage_{};
    std::array<std::array<std::string, kProbes>, StageCount> previous_region_;
    std::filesystem::path directory_;
    std::ofstream csv_;
    std::ofstream presents_;
    std::ofstream stages_, tiles_, events_;
    std::string floor_;
    std::optional<glm::vec3> fixture_origin_;
    bool enhanced_ = false, menu_ = false;
    double display_sample_time_ = -1;
    bool display_menu_ = false;
    std::unordered_set<uint64_t> named_lights_;
    uint64_t sequence_ = 0;
    uint64_t segment_ = UINT64_MAX;
    bool warned_ = false;
    bool failed_ = false;
    bool stage_failed_ = false;
};

}
