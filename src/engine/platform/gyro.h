#pragma once

#include <cmath>

#include <glm/glm.hpp>

namespace pt {

class GyroTracker {
public:
    static constexpr glm::vec2 kAimLimit{0.6435011f, 0.5126525f};
    static constexpr float kWindow = 0.1f;
    static constexpr float kStillSpread = 0.045f;
    static constexpr float kRestGate = 0.05f;
    static constexpr float kStillTime = 0.3f;
    static constexpr float kLearnRate = 3.0f;
    static constexpr float kMaxBias = 0.35f;
    static constexpr float kReturnTime = 0.35f;
    static constexpr float kCalibrationTime = 3.0f;
    static constexpr float kCalibrationSpread = 0.1f;
    static constexpr float kDefaultTightening = 0.0698132f;

    enum class Calibration { None, Running, Done, Failed };

    void Add(const glm::vec3& rate, float dt, float aim_scale = 1.0f);
    glm::vec2 TakeTurn();
    glm::vec2 Aim() const { return aim_; }
    glm::vec2 AimStick() const;
    void Recenter() { aim_ = glm::vec2(0.0f); }
    void Reset();
    bool Still() const { return rest_; }
    glm::vec3 Offset() const { return bias_; }
    bool Learned() const { return learned_; }
    void SetOffset(const glm::vec3& offset);
    void ClearOffset();
    void SetTightening(float rate) { tightening_ = std::isfinite(rate) && rate > 0.0f ? rate : 0.0f; }
    void StartCalibration();
    void ClearCalibration() { if (calibration_ != Calibration::Running) calibration_ = Calibration::None; }
    Calibration CalibrationState() const { return calibration_; }
    float CalibrationProgress() const;
    bool TakeCalibrated(glm::vec3& offset);
    unsigned Samples() const { return samples_; }

private:
    void AddCalibration(const glm::vec3& rate, float dt);
    void EndWindow();

    glm::vec3 bias_{0.0f};
    glm::vec2 turn_{0.0f};
    glm::vec2 aim_{0.0f};
    float calm_ = 0.0f;
    bool rest_ = false;
    bool learned_ = false;
    float tightening_ = kDefaultTightening;
    glm::vec3 window_sum_{0.0f};
    glm::vec3 window_min_{0.0f};
    glm::vec3 window_max_{0.0f};
    float window_time_ = 0.0f;
    Calibration calibration_ = Calibration::None;
    glm::vec3 cal_sum_{0.0f};
    glm::vec3 cal_min_{0.0f};
    glm::vec3 cal_max_{0.0f};
    float cal_time_ = 0.0f;
    bool cal_ready_ = false;
    glm::vec3 cal_result_{0.0f};
    unsigned samples_ = 0;
};

}
