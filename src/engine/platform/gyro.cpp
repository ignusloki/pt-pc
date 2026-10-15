#include "engine/platform/gyro.h"

#include <algorithm>
#include <cmath>

namespace pt {

void GyroTracker::Add(const glm::vec3& rate, float dt, float aim_scale) {
    if (!(dt > 0.0f) || !std::isfinite(rate.x) || !std::isfinite(rate.y) || !std::isfinite(rate.z)) {
        return;
    }
    ++samples_;
    const bool resting = glm::length(rate - bias_) < kStillRate;
    if (resting) {
        still_ += dt;
        if (still_ >= kStillTime) {
            bias_ += (rate - bias_) * std::min(1.0f, dt * 2.0f);
        }
    } else {
        still_ = 0.0f;
    }
    const glm::vec3 corrected = resting ? glm::vec3(0.0f) : rate - bias_;
    const glm::vec2 step(corrected.y * dt, corrected.x * dt);
    turn_ += step;
    aim_ += step * aim_scale;
    aim_ = glm::clamp(aim_, -kAimLimit, kAimLimit);
    if (Still()) {
        aim_ *= std::exp(-dt / kReturnTime);
    }
}

glm::vec2 GyroTracker::TakeTurn() {
    const glm::vec2 turn = turn_;
    turn_ = glm::vec2(0.0f);
    return turn;
}

glm::vec2 GyroTracker::AimStick() const {
    return glm::clamp(glm::vec2(-std::tan(aim_.x) / 0.75f, -std::tan(aim_.y) / 0.5625f), glm::vec2(-1.0f), glm::vec2(1.0f));
}

void GyroTracker::Reset() {
    turn_ = glm::vec2(0.0f);
    aim_ = glm::vec2(0.0f);
    still_ = 0.0f;
}

}
