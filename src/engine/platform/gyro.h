#pragma once

#include <glm/glm.hpp>

namespace pt {

class GyroTracker {
public:
    static constexpr glm::vec2 kAimLimit{0.6435011f, 0.5126525f};
    static constexpr float kStillRate = 0.06f;
    static constexpr float kStillTime = 0.3f;
    static constexpr float kReturnTime = 0.35f;

    void Add(const glm::vec3& rate, float dt, float aim_scale = 1.0f);
    glm::vec2 TakeTurn();
    glm::vec2 Aim() const { return aim_; }
    glm::vec2 AimStick() const;
    void Recenter() { aim_ = glm::vec2(0.0f); }
    void Reset();
    bool Still() const { return still_ >= kStillTime; }
    glm::vec3 Offset() const { return bias_; }
    unsigned Samples() const { return samples_; }

private:
    glm::vec3 bias_{0.0f};
    glm::vec2 turn_{0.0f};
    glm::vec2 aim_{0.0f};
    float still_ = 0.0f;
    unsigned samples_ = 0;
};

}
