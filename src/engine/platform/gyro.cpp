#include "engine/platform/gyro.h"

#include <algorithm>
#include <cmath>

namespace pt {

namespace {

float MaxComponent(const glm::vec3& v) { return std::max({v.x, v.y, v.z}); }

}

void GyroTracker::Add(const glm::vec3& rate, float dt, float aim_scale) {
    if (!(dt > 0.0f) || !std::isfinite(rate.x) || !std::isfinite(rate.y) || !std::isfinite(rate.z)) {
        return;
    }
    ++samples_;
    if (calibration_ == Calibration::Running) {
        AddCalibration(rate, dt);
        return;
    }
    if (window_time_ <= 0.0f) {
        window_min_ = window_max_ = rate;
        window_sum_ = glm::vec3(0.0f);
    }
    window_min_ = glm::min(window_min_, rate);
    window_max_ = glm::max(window_max_, rate);
    window_sum_ += rate * dt;
    window_time_ += dt;
    if (window_time_ >= kWindow) {
        EndWindow();
    }
    const glm::vec3 off = rate - bias_;
    const float length = glm::length(off);
    rest_ = calm_ >= kStillTime && length < kRestGate;
    glm::vec3 corrected(0.0f);
    if (!(length < kRestGate && (rest_ || !learned_))) {
        corrected = off;
        if (tightening_ > 0.0f && length < tightening_) {
            corrected *= length / tightening_;
        }
    }
    const glm::vec2 step(corrected.y * dt, corrected.x * dt);
    turn_ += step;
    aim_ += step * aim_scale;
    aim_ = glm::clamp(aim_, -kAimLimit, kAimLimit);
    if (rest_) {
        aim_ *= std::exp(-dt / kReturnTime);
    }
}

void GyroTracker::EndWindow() {
    const glm::vec3 mean = window_sum_ / window_time_;
    const float time = window_time_;
    const bool still = MaxComponent(window_max_ - window_min_) < kStillSpread && glm::length(mean) < kMaxBias;
    window_time_ = 0.0f;
    if (!still) {
        calm_ = 0.0f;
        return;
    }
    calm_ += time;
    if (calm_ >= kStillTime) {
        if (!learned_) {
            bias_ = mean;
            learned_ = true;
        } else {
            bias_ += (mean - bias_) * std::min(1.0f, time * kLearnRate);
        }
    }
}

void GyroTracker::AddCalibration(const glm::vec3& rate, float dt) {
    if (cal_time_ <= 0.0f) {
        cal_min_ = cal_max_ = rate;
        cal_sum_ = glm::vec3(0.0f);
    }
    cal_min_ = glm::min(cal_min_, rate);
    cal_max_ = glm::max(cal_max_, rate);
    cal_sum_ += rate * dt;
    cal_time_ += dt;
    if (MaxComponent(cal_max_ - cal_min_) >= kCalibrationSpread) {
        calibration_ = Calibration::Failed;
        return;
    }
    if (cal_time_ >= kCalibrationTime) {
        const glm::vec3 mean = cal_sum_ / cal_time_;
        if (glm::length(mean) >= kMaxBias) {
            calibration_ = Calibration::Failed;
            return;
        }
        bias_ = mean;
        learned_ = true;
        cal_result_ = mean;
        cal_ready_ = true;
        calibration_ = Calibration::Done;
        calm_ = 0.0f;
        window_time_ = 0.0f;
    }
}

void GyroTracker::StartCalibration() {
    calibration_ = Calibration::Running;
    cal_time_ = 0.0f;
    cal_ready_ = false;
    turn_ = glm::vec2(0.0f);
    aim_ = glm::vec2(0.0f);
    rest_ = false;
}

float GyroTracker::CalibrationProgress() const {
    if (calibration_ == Calibration::Done) return 1.0f;
    if (calibration_ != Calibration::Running) return 0.0f;
    return std::clamp(cal_time_ / kCalibrationTime, 0.0f, 1.0f);
}

bool GyroTracker::TakeCalibrated(glm::vec3& offset) {
    if (!cal_ready_) return false;
    cal_ready_ = false;
    offset = cal_result_;
    return true;
}

void GyroTracker::SetOffset(const glm::vec3& offset) {
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)) return;
    bias_ = offset;
    learned_ = true;
}

void GyroTracker::ClearOffset() {
    bias_ = glm::vec3(0.0f);
    learned_ = false;
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
    calm_ = 0.0f;
    rest_ = false;
    window_time_ = 0.0f;
    if (calibration_ == Calibration::Running) calibration_ = Calibration::None;
}

}
