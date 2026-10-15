#ifdef NDEBUG
#undef NDEBUG
#endif
#include "game/archive_model_camera.h"
#include "engine/render/free_camera.h"

#include <cassert>
#include <cmath>

using pt::game::PanArchiveModel;

int main() {
    const glm::vec3 home(1.0f, 2.0f, 3.0f);
    const glm::vec3 right(1.0f, 0.0f, 0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);

    const glm::vec3 centered = PanArchiveModel(home, home, right, up, glm::vec2(0.0f), 1.0f, 2.0f, 1.0f);
    assert(centered == home);

    const glm::vec3 horizontal = PanArchiveModel(home, home, right, up, glm::vec2(1.0f, 0.0f), 0.5f, 2.0f, 1.0f);
    assert(horizontal.x > home.x && horizontal.y == home.y && horizontal.z == home.z);

    const glm::vec3 vertical = PanArchiveModel(home, home, right, up, glm::vec2(0.0f, -1.0f), 0.5f, 2.0f, 1.0f);
    assert(vertical.y > home.y && vertical.x == home.x && vertical.z == home.z);

    const glm::vec3 bounded = PanArchiveModel(home, home, right, up, glm::vec2(1.0f, -1.0f), 100.0f, 20.0f, 1.0f);
    assert(std::abs(glm::length(bounded - home) - 1.5f) < 1e-5f);

    pt::Camera fly;
    fly.position = glm::vec3(0.0f);
    fly.yaw = fly.pitch = 0.0f;
    pt::FreeCameraControls controls;
    controls.movement.y = 1.0f;
    pt::AdvanceFreeCamera(fly, controls, 1.0f, false, false);
    assert(glm::distance(fly.position, glm::vec3(0.0f, 0.0f, -1.5f)) < 1e-5f);
    controls = {};
    controls.mouse_look = {0.4f, -0.2f};
    pt::AdvanceFreeCamera(fly, controls, 0.0f, false, false);
    assert(std::abs(fly.yaw + 0.4f) < 1e-5f && std::abs(fly.pitch - 0.2f) < 1e-5f);
    controls = {};
    controls.rise = 1.0f;
    controls.fast = true;
    pt::AdvanceFreeCamera(fly, controls, 1.0f, false, false);
    assert(std::abs(fly.position.y - 6.0f) < 1e-5f);
    fly.position = {100.0f, 0.0f, 0.0f};
    pt::LimitFreeCameraRange(fly, glm::vec3(0.0f), 15.0f);
    assert(std::abs(glm::length(fly.position) - 15.0f) < 1e-5f);

    const glm::vec3 invalid_dt = PanArchiveModel(home, home, right, up, glm::vec2(1.0f), 0.0f, 2.0f, 1.0f);
    assert(invalid_dt == home);

    assert(std::abs(pt::game::ArchiveModelFrontYaw("radio", 0.4f) - (0.4f + glm::radians(90.0f))) < 1e-5f);
    assert(pt::game::ArchiveModelFrontYaw("lisa_stand", 0.4f) == 0.4f);

    pt::SceneLighting lighting;
    lighting.lights.emplace_back().intensity = glm::vec3(100000.0f);
    lighting.probes.emplace_back();
    lighting.exposure.compensation = 9.0f;
    for (float radius : {0.05f, 0.5f, 2.0f}) {
        for (float yaw : {0.0f, glm::radians(90.0f), glm::radians(180.0f)}) {
            pt::Camera camera;
            camera.yaw = yaw;
            camera.position = home - camera.Forward() * radius * 2.0f;
            pt::game::LightArchiveModel(lighting, camera, home, radius, pt::game::kArchiveModelEv);
            assert(lighting.valid && lighting.lights.size() == 3 && lighting.probes.empty());
            assert(lighting.exposure.pinned && lighting.exposure.pinned_ev == pt::game::kArchiveModelEv);
            assert(lighting.exposure.compensation == 0.0f && lighting.exposure.bloom_weight == 0.0f);
            for (float compensation : lighting.exposure.add_comp) assert(compensation == 0.0f);
            assert(!lighting.screen.depth_of_field && !lighting.screen.motion_blur);
            float front_light = 0.0f;
            for (const auto& light : lighting.lights) {
                const glm::vec3 to_light = light.position - home;
                const float distance2 = glm::dot(to_light, to_light);
                const float attenuation = 1.0f / distance2 - distance2 / std::pow(light.outer_range, 4.0f);
                front_light += light.intensity.x * attenuation * std::max(0.0f, glm::dot(glm::normalize(to_light), -camera.Forward()));
                assert(light.id != pt::kHandyLightId && light.specular_scale < 1.0f);
            }
            const float exposed_front = front_light * std::exp2(lighting.exposure.pinned_ev);
            assert(exposed_front > 0.4f && exposed_front < 1.0f);
        }
    }
}
