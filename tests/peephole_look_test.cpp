#include <cmath>
#include <cstdio>
#include "engine/fs/vfs.h"
#include "game/player.h"
#include "game/player_animation.h"

int main(int argc, char** argv) {
    using namespace pt;
    using namespace pt::game;
    int failures = 0;
    const auto check = [&](const char* name, bool ok) {
        std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
        failures += !ok;
    };
    Player player;
    CollisionWorld world;
    PlayerFrameContext context;
    InputState input;
    player.FollowCamera(3.1f, 0.0f);
    context.peephole_theater = true;
    player.Update(1.0f / 60, input, world, context);
    const float center = player.yaw;
    const auto bounded = [&] {
        return std::abs(std::remainder(player.yaw - center, 6.2831853f)) <= .261801f &&
               std::abs(player.pitch) <= .261801f &&
               std::abs(std::remainder(player.target_yaw - center, 6.2831853f)) <= .261801f &&
               std::abs(player.target_pitch) <= .261801f;
    };
    for (float delta : {1000.0f, -1000.0f, 3.1415927f, -3.1415927f}) {
        input.mouse_look = glm::vec2(delta, delta);
        player.Update(1.0f / 60, input, world, context);
        check("large mouse turn stays inside aperture, including yaw seam", bounded());
    }
    input = InputState{};
    input.held = kPadLookRight | kPadLookUp;
    for (int i = 0; i < 240; ++i) player.Update(1.0f / 60, input, world, context);
    check("held keyboard look stays inside aperture", bounded());
    {
        float turned_yaw = center + 1.0f;
        float turned_pitch = -1.0f;
        player.ClampPeepholeLook(turned_yaw, turned_pitch);
        check("render turn beyond the aperture stops at its edge",
              std::abs(std::remainder(turned_yaw - center, 6.2831853f) - .2617994f) < 1e-4f && std::abs(turned_pitch + .2617994f) < 1e-6f);
        float inside_yaw = center + 0.1f;
        float inside_pitch = 0.05f;
        player.ClampPeepholeLook(inside_yaw, inside_pitch);
        check("render turn inside the aperture is kept", inside_yaw == center + 0.1f && inside_pitch == 0.05f);
    }
    context.peephole_theater = false;
    input = InputState{};
    input.mouse_look.x = 1.0f;
    player.Update(1.0f / 60, input, world, context);
    check("ending theater restores ordinary free look", !bounded());
    input = InputState{};
    context.peephole_theater = true;
    const float reentry = player.yaw;
    player.Update(1.0f / 60, input, world, context);
    check("re-entering does not reuse old aperture heading", std::abs(player.yaw - reentry) < .0001f);
    Vfs vfs;
    if (argc != 2 || !vfs.Mount(argv[1]) || !vfs.LoadPackage("/Assets/sh/level/common/resident.fpk") ||
        !LoadPlayerAnimation(vfs) || !PlayerLocomotionData().loaded) {
        std::puts("needs the game folder for native body/camera entry");
        return 2;
    }
    {
        Player diagonal;
        diagonal.camera_roll = 0.0f;
        context.peephole_center_valid = true;
        context.peephole_center_yaw = glm::radians(-90.0f);
        diagonal.Warp(glm::vec3(0.0f), glm::radians(37.06f));
        diagonal.FollowCamera(glm::radians(-192.3312f), glm::radians(-48.035f));
        diagonal.Update(1.0f / 60, {}, world, context);
        check("diagonal entry follows native quaternion path", std::abs(glm::degrees(diagonal.yaw) + 179.21f) < .03f &&
              std::abs(glm::degrees(diagonal.pitch) + 38.443f) < .03f);
        context.peephole_theater = false;
        InputState head;
        head.vr_look = true;
        head.vr_look_angles = glm::vec2(0.1f, 0.2f);
        diagonal.Update(1.0f / 60, head, world, context);
        check("VR head tracking clears peephole roll immediately on abort", std::abs(diagonal.MakeCamera().roll) < 1e-6f);
        context.peephole_theater = true;
    }
    constexpr float ps4_yaw[] = {34.0627f, 34.0627f, 34.0627f, 33.2094f, 31.4209f, 29.3863f,
                               27.4133f, 25.6246f, 24.0569f, 22.7007f, 21.5414f, 20.5559f,
                               19.7222f, 19.0169f, 18.4217f, 17.9260f, 17.5121f, 17.1656f};
    for (float sign : {-1.0f, 1.0f}) {
        Player entry;
        context.peephole_center_valid = true;
        context.peephole_center_yaw = 3.1f;
        const float start = context.peephole_center_yaw + sign * glm::radians(ps4_yaw[0]);
        entry.Warp(glm::vec3(0.0f), start + 3.14159265f);
        entry.FollowCamera(start, 0.0f);
        float max_error = 0.0f;
        int sample = 0;
        for (float expected : ps4_yaw) {
            entry.Update(1.0f / 30, {}, world, context);
            const float actual = sign * glm::degrees(std::remainder(entry.yaw - context.peephole_center_yaw, 6.2831853f));
            max_error = std::max(max_error, std::abs(actual - expected));
            if (sign > 0.0f) std::printf("response,%d,%.4f,%.4f\n", sample++, expected, actual);
        }
        std::printf("PS4 response maximum yaw error: %.4f degrees\n", max_error);
        check("oblique entry follows the PS4 body and look response across the yaw seam", max_error < 0.6f);
        check("render without a pending turn preserves the entry response", entry.PeepholeTurnDelta({}) == glm::vec2(0.0f));
        context.peephole_theater = false;
        entry.Update(1.0f / 30, {}, world, context);
        entry.Warp(glm::vec3(0.0f), start + 3.14159265f);
        entry.FollowCamera(start, 0.0f);
        context.peephole_theater = true;
        entry.Update(1.0f / 30, {}, world, context);
        check("reentry starts at its new approach angle without a snap", std::abs(std::remainder(entry.yaw - start, 6.2831853f)) < 1e-4f);
    }
    return failures ? 1 : 0;
}
