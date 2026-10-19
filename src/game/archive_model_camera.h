#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

#include <glm/glm.hpp>

#include "engine/render/camera.h"
#include "engine/render/scene_lighting.h"

namespace pt::game {

inline constexpr float kArchiveModelEv = 0.0f;

inline float ArchiveModelFrontYaw(std::string_view id, float placement_yaw) {
    return placement_yaw + (id == "radio" ? glm::radians(90.0f) : 0.0f);
}

inline void LightArchiveModel(SceneLighting& lighting, const Camera& camera, const glm::vec3& center,
                              float radius, float ev) {
    lighting = SceneLighting{};
    lighting.valid = true;
    const float reach = std::max(radius, 0.05f) * 4.0f;
    const glm::vec3 toward_camera = -camera.Forward();
    const glm::vec3 offsets[] = {
        toward_camera - camera.Right() * 0.65f + camera.Up() * 0.8f,
        toward_camera + camera.Right() * 0.9f + camera.Up() * 0.15f,
        -toward_camera + camera.Up() * 0.8f,
    };
    const float strengths[] = {0.65f, 0.3f, 0.25f};
    for (size_t i = 0; i < 3; ++i) {
        SceneLight light;
        light.id = 0x4D555345554D0000ull + i;
        light.name = "Museum";
        light.position = center + glm::normalize(offsets[i]) * reach;
        light.intensity = glm::vec3(strengths[i] * reach * reach);
        light.inner_range = reach * 0.1f;
        light.outer_range = reach * 4.0f;
        light.source_radius = reach * 0.15f;
        light.specular_scale = 0.35f;
        lighting.lights.push_back(light);
    }
    lighting.exposure.pinned = true;
    lighting.exposure.pinned_ev = ev;
    lighting.exposure.compensation = 0.0f;
    for (float& compensation : lighting.exposure.add_comp) compensation = 0.0f;
    lighting.exposure.bloom_weight = 0.0f;
    lighting.screen.color_scale = glm::vec3(1.0f);
    lighting.screen.film_grain = false;
    lighting.screen.screen_distortion = false;
    lighting.screen.depth_of_field = false;
    lighting.screen.motion_blur = false;
    lighting.screen.local_reflections = false;
}

inline glm::vec3 PanArchiveModel(const glm::vec3& target, const glm::vec3& home, const glm::vec3& right,
                                 const glm::vec3& up, glm::vec2 input, float dt, float distance, float radius) {
    if (!(dt > 0.0f) || !std::isfinite(dt)) return target;
    const float extent = std::max(radius, 0.05f);
    const glm::vec3 direction = right * input.x - up * input.y;
    const glm::vec3 moved = target + direction * (std::max(distance, extent) * 0.8f * dt);
    const glm::vec3 offset = moved - home;
    const float limit = extent * 1.5f;
    const float length = glm::length(offset);
    return length > limit ? home + offset * (limit / length) : moved;
}

}
