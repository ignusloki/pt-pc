#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pt {
class ModelCache;
struct GpuMesh;
struct DrawItem;
}

namespace pt::game {

struct LisaPoseFit {
    std::vector<glm::mat4> skin;
    size_t matched = 0;
    size_t count = 0;
    double mean = 0.0;
    bool ok = false;
};
LisaPoseFit FitLisaStaticPose(ModelCache& models, std::string_view baked_path,
                              std::span<const std::pair<std::string_view, std::string_view>> materials, double min_share, double max_mean,
                              std::string_view what);

class GamePlusArm {
public:
    bool Prepare(ModelCache& models);
    void Apply(std::vector<DrawItem>& out);

private:
    bool ready_ = false;
    bool tried_ = false;
    const GpuMesh* lisa_mesh_ = nullptr;
    const GpuMesh* lisa_cut_ = nullptr;
    const GpuMesh* side_mesh_ = nullptr;
    std::vector<glm::vec3> lisa_bind_;
    std::vector<glm::vec3> side_bind_;
    std::vector<glm::vec3> side_retarget_;
    std::vector<int> side_parent_;
    std::vector<int> side_source_;
    std::vector<bool> side_arm_;
    float scale_ = 0.6f;
    glm::vec3 shift_{0.0f};
    std::vector<int> side_region_;
    float arm_lengths_[3] = {0.0f, 0.0f, 0.0f};
    std::deque<std::vector<glm::mat4>> skins_;
    const GpuMesh* static_mesh_ = nullptr;
    std::vector<glm::mat4> static_skin_;
    bool static_fitted_ = false;
    uint32_t static_probe_ = 0;
    void FitStaticPose(ModelCache& models);
};

}
