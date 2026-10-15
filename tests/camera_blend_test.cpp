#include "engine/render/camera.h"

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

bool Near(float a, float b, float eps = 1e-5f) {
    return std::abs(a - b) <= eps;
}
}

int main() {
    pt::Camera from;
    from.position = {0.70f, 0.30f, 8.30f};
    from.yaw = glm::radians(-176.4f);
    from.pitch = glm::radians(-48.4f);
    from.roll = glm::radians(-61.0f);
    from.fov_y = glm::radians(50.0f);
    pt::Camera to = from;
    to.position += glm::vec3(0.002f, 0.003f, 0.001f);
    to.yaw = glm::radians(-176.2f);
    to.pitch = glm::radians(-49.0f);
    to.roll = glm::radians(-61.9f);
    bool cut = true;
    const pt::Camera half = pt::BlendCameras(from, to, 0.5f, &cut);
    Check(!cut, "a tick of the get-up is not a cut");
    Check(Near(half.roll, glm::radians(-61.45f)), "roll halfway");
    Check(Near(half.pitch, glm::radians(-48.7f)), "pitch halfway");
    Check(Near(half.yaw, glm::radians(-176.3f)), "yaw halfway");
    Check(glm::length(half.position - (from.position + to.position) * 0.5f) < 1e-5f, "position halfway");
    float last = -1.0f;
    float lo = 1e9f;
    float hi = 0.0f;
    glm::vec3 up_before = pt::BlendCameras(from, to, 0.0f).Up();
    for (int i = 1; i <= 8; ++i) {
        const glm::vec3 up = pt::BlendCameras(from, to, i / 8.0f).Up();
        const float step = std::acos(std::fmin(1.0f, glm::dot(up, up_before)));
        lo = std::fmin(lo, step);
        hi = std::fmax(hi, step);
        up_before = up;
        last = step;
    }
    Check(last > 0.0f && hi < lo * 1.2f, "the up vector turns evenly between ticks");
    from.roll = glm::radians(179.0f);
    to = from;
    to.roll = glm::radians(-179.0f);
    const pt::Camera wrap = pt::BlendCameras(from, to, 0.5f, &cut);
    Check(!cut && Near(std::abs(std::remainder(wrap.roll - glm::radians(180.0f), 2.0f * 3.14159265f)), 0.0f, 1e-4f),
          "roll halfway across 180 degrees");
    to.roll = from.roll + glm::radians(40.0f);
    const pt::Camera jump = pt::BlendCameras(from, to, 0.5f, &cut);
    Check(cut && jump.roll == to.roll, "a roll jump is a cut");
    pt::Camera a;
    a.yaw = 3.1f;
    a.pitch = 0.1f;
    a.fov_y = 1.0f;
    pt::Camera b = a;
    b.yaw = -3.1f;
    b.pitch = 0.2f;
    b.fov_y = 1.2f;
    const pt::Camera ab = pt::BlendCameras(a, b, 0.25f, &cut);
    Check(!cut && Near(std::remainder(ab.yaw - (3.1f + 0.25f * (2.0f * 3.14159265f - 6.2f)), 2.0f * 3.14159265f), 0.0f, 1e-4f), "yaw the short way");
    Check(Near(ab.pitch, 0.125f) && Near(ab.fov_y, 1.05f) && ab.roll == 0.0f, "pitch, angle and no roll");
    std::printf("camera blend: %d failures\n", failures);
    return failures ? 1 : 0;
}
