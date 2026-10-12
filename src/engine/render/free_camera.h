#pragma once

#include <algorithm>
#include "engine/render/camera.h"

namespace pt {
struct FreeCameraControls {
    glm::vec2 movement{0.0f};
    glm::vec2 mouse_look{0.0f};
    glm::vec2 right_stick{0.0f};
    float rise = 0.0f;
    bool fast = false;
};

inline void AdvanceFreeCamera(Camera& camera, const FreeCameraControls& input, float dt,
                              bool invert_x, bool invert_y, float speed_scale = 1.0f) {
    const glm::vec3 move = camera.Forward() * input.movement.y + camera.Right() * input.movement.x +
                           glm::vec3(0.0f, input.rise, 0.0f);
    camera.position += move * ((input.fast ? 6.0f : 1.5f) * speed_scale * dt);
    const glm::vec2 look = input.mouse_look + input.right_stick * (2.5f * dt);
    camera.yaw -= look.x * (invert_x ? -1.0f : 1.0f);
    camera.pitch = std::clamp(camera.pitch - look.y * (invert_y ? -1.0f : 1.0f), -1.55f, 1.55f);
}

inline void LimitFreeCameraRange(Camera& camera, const glm::vec3& origin, float range) {
    const glm::vec3 offset = camera.position - origin;
    const float distance = glm::length(offset);
    if (distance > range) camera.position = origin + offset * (range / distance);
}
}
