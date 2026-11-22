#pragma once

#include "engine/render/vk_context.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <string>
#include <unordered_set>

namespace pt {

struct Camera;
struct RenderStats;
struct SceneLighting;
namespace vfx { struct RenderList; }

// Read small patches of the finished scRGB images using the renderer's existing frame fences.
class LampTrace {
public:
    explicit LampTrace(std::filesystem::path directory);
    void Scene(uint64_t tick, const std::string& floor, const Camera& camera, const SceneLighting& lights,
               const RenderStats& stats, const vfx::RenderList& effects, float aspect, bool lens, float paper, float headroom);
    void Capture(vk::Context& ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkExtent2D extent,
                 uint32_t slot, bool interpolated, bool repeat);
    void FinishSlot(vk::Context& ctx, uint32_t slot);
    void Shutdown(vk::Context& ctx);

private:
    static constexpr unsigned kRegions = 12, kSide = 48;
    struct Region {
        std::string name;
        glm::vec2 uv;
        float depth = 0, alpha = 0;
        uint64_t light_id = 0;
        float light_intensity = 0;
    };
    struct Sample {
        std::array<Region, kRegions> regions;
        unsigned count = 0;
        std::string metadata;
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
    std::filesystem::path directory_;
    std::ofstream csv_;
    std::unordered_set<uint64_t> named_lights_;
    uint64_t sequence_ = 0;
    uint64_t segment_ = UINT64_MAX;
    bool warned_ = false;
    bool failed_ = false;
};

}
