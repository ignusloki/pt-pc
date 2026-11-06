#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/render/camera.h"

namespace pt {
struct Renderer {
    bool enhanced = false;
    bool EnhancedRendering() const { return enhanced; }
};

struct SceneRenderer {
    static constexpr float kCameraCutDistance = 3.0f;
    Renderer renderer;
    Renderer* renderer_ = &renderer;
    struct Lighting { struct Screen { bool smooth_camera_motion_blur = false; } screen; } lighting;
    Lighting* lighting_ = &lighting;
    struct { bool reset = false; } up_;
    struct { unsigned width = 1920, height = 1080; } output_extent_;
    Camera motion_reference_, last_camera_;
    float motion_interval_ = 1.0f / 60.0f;
    bool motion_reference_set_ = false, motion_reference_history_ = true;
    bool motion_history_valid_ = false, has_last_camera_ = false, blur_rates_valid_ = false;
    glm::mat4 previous_view_projection_{1.0f}, blur_previous_view_projection_{1.0f};
    glm::vec4 blur_rates_{0.0f};
    glm::vec3 blur_velocity_{0.0f};
    double frame_time_ = 1.0 / 60.0;
    void UpdateMotion(const Camera&, float);
};

#include "motion_update_under_test.inc"
}

int CheckMotion(bool enhanced) {
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s: %s: %s\n", enhanced ? "Enhanced" : "Original", name, ok ? "PASS" : "FAIL");
        failures += !ok;
    };
    auto close = [](const glm::mat4& a, const glm::mat4& b) {
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > 1e-6f) return false;
        return true;
    };
    constexpr float dt = 1.0f / 60.0f;
    pt::SceneRenderer scene;
    scene.renderer.enhanced = enhanced;
    pt::Camera camera;
    scene.UpdateMotion(camera, dt);
    camera.yaw += 0.04f;
    camera.position.x += 0.02f;
    scene.UpdateMotion(camera, dt);
    camera.yaw += 0.01f;
    camera.position.x += 0.01f;
    scene.UpdateMotion(camera, dt);
    check("indoors uses the exact previous camera during changing motion",
          close(scene.blur_previous_view_projection_, scene.previous_view_projection_));
    scene.UpdateMotion(camera, dt);
    check("indoors has no residual blur on the first stopped frame",
          close(scene.blur_previous_view_projection_, camera.Projection(1920.0f / 1080.0f) * camera.View()));
    scene.lighting.screen.smooth_camera_motion_blur = true;
    camera.yaw += 0.06f;
    scene.UpdateMotion(camera, dt);
    check("entering the street initializes fresh rates", std::abs(scene.blur_rates_.x - 3.6f) < 1e-4f);
    camera.yaw += 0.01f;
    scene.UpdateMotion(camera, dt);
    const float expected = 3.6f + (0.6f - 3.6f) * (1.0f - std::exp(-dt * 30.0f));
    check("street smooths the camera over one 30 Hz frame", std::abs(scene.blur_rates_.x - expected) < 1e-4f);
    const float turning = scene.blur_rates_.x;
    for (int i = 0; i < 6; ++i) {
        scene.UpdateMotion(camera, dt);
    }
    check("street blur has decayed to 5 percent 100 ms after the view stops", std::abs(scene.blur_rates_.x) < 0.0501f * turning);
    scene.lighting.screen.smooth_camera_motion_blur = false;
    scene.UpdateMotion(camera, dt);
    check("leaving the street immediately discards residual blur",
          close(scene.blur_previous_view_projection_, scene.previous_view_projection_) && !scene.blur_rates_valid_);
    scene.lighting.screen.smooth_camera_motion_blur = true;
    scene.motion_reference_set_ = true;
    scene.motion_reference_ = camera;
    scene.motion_reference_.yaw -= 0.02f;
    scene.UpdateMotion(camera, dt);
    check("explicit tick references keep exact motion and clear filtered history",
          close(scene.blur_previous_view_projection_, scene.previous_view_projection_) && !scene.blur_rates_valid_);
    camera.position.x += 4.0f;
    scene.UpdateMotion(camera, dt);
    check("camera cuts have no filtered motion", scene.up_.reset && glm::length(scene.blur_rates_) == 0.0f &&
          glm::length(scene.blur_velocity_) == 0.0f && close(scene.blur_previous_view_projection_, scene.previous_view_projection_));
    check("camera cuts invalidate temporal history only in Enhanced", scene.motion_history_valid_ == !enhanced);

    pt::SceneRenderer missing_history;
    missing_history.renderer.enhanced = enhanced;
    missing_history.motion_reference_set_ = true;
    missing_history.motion_reference_history_ = false;
    missing_history.motion_reference_ = camera;
    missing_history.motion_reference_.yaw -= 0.02f;
    missing_history.UpdateMotion(camera, dt);
    const pt::Camera& previous = enhanced ? camera : missing_history.motion_reference_;
    check("missing history resets the previous camera only in Enhanced",
          missing_history.up_.reset && !missing_history.motion_history_valid_ &&
          close(missing_history.previous_view_projection_, previous.Projection(1920.0f / 1080.0f) * previous.View()));

    pt::SceneRenderer timing;
    timing.renderer.enhanced = enhanced;
    timing.UpdateMotion(camera, dt * 2.0f);
    check("Enhanced uses the current frame interval; Original smooths it",
          enhanced ? std::abs(timing.frame_time_ - dt * 2.0f) < 1e-8 : timing.frame_time_ > dt && timing.frame_time_ < dt * 2.0f);
    timing.motion_reference_set_ = true;
    timing.motion_reference_ = camera;
    timing.motion_interval_ = dt * 0.5f;
    timing.UpdateMotion(camera, dt * 2.0f);
    check("explicit tick references use their own frame interval", std::abs(timing.frame_time_ - dt * 0.5f) < 1e-8);
    return failures;
}

int main() {
    int failures = CheckMotion(false);
    failures += CheckMotion(true);
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
