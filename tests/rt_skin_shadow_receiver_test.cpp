#include <glm/glm.hpp>
#include <cstdio>

using uint = unsigned;
using glm::vec2;
using glm::vec3;
using glm::normalize;

#include "../shaders/rt_skin_shadow_receiver.glsl"

int main() {
    unsigned cases = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++cases;
        if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
    };
    const vec2 authored(-0.005f, -0.005f);
    check(RtSkinUsesNativeReceiver(10, true, true, false, authored), "skin receives its authored spot offsets");
    check(!RtSkinUsesNativeReceiver(40, true, true, false, authored), "room materials keep geometric offset");
    check(!RtSkinUsesNativeReceiver(11, true, true, false, authored), "eyes keep geometric offset");
    check(!RtSkinUsesNativeReceiver(10, false, true, false, authored), "point projection unchanged");
    check(!RtSkinUsesNativeReceiver(10, true, false, false, authored), "added fill shadows unchanged");
    check(!RtSkinUsesNativeReceiver(10, true, true, true, authored), "mirror light unchanged");
    check(!RtSkinUsesNativeReceiver(10, true, true, false, vec2(0)), "zero authored bias retains roundoff offset");
    const vec3 world(1, 2, 3), forward(0, 0, 1), axis(0, -1, 0);
    check(glm::length(RtSkinNativeReceiver(world, forward, axis, authored) - vec3(1, 2.005f, 2.995f)) < 1e-6f,
          "native receiver uses light and view translations once");
    check(glm::length(RtSkinNativeReceiver(world, forward, axis * 5.0f, authored) - vec3(1, 2.005f, 2.995f)) < 1e-6f,
          "light axis normalized as native projection");
    check(glm::length(RtSkinNativeReceiver(world, forward, axis, vec2(0)) - world) < 1e-6f, "zero translation is identity");
    std::printf("skin shadow receiver: %u cases, %u failures\n", cases, failures);
    return failures ? 1 : 0;
}
