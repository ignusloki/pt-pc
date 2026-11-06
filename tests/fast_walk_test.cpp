#include "check.h"
#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "engine/fs/vfs.h"
#include "engine/platform/input.h"
#include "engine/platform/virtual_pad.h"
#include "game/player.h"
#include "game/player_animation.h"

namespace {

int failures = 0;


bool Near(float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) <= tolerance; }

void CheckInput() {
    pt::InputDevice device;
    device.settings.rumble = false;
    device.Init();
    pt::VirtualPads pads;
    auto poll = [&](bool pads_free = true) {
        SDL_UpdateGamepads();
        SDL_Event event;
        while (SDL_PollEvent(&event)) device.ProcessEvent(event);
        return device.Poll(false, pt::MouseUse::Look, pads_free);
    };
    pt::test::CheckNamed(failures, "no input requests fast walk", !poll().fast_walk);
    for (SDL_Scancode key : {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}) {
        device.InjectKey(key, true);
        pt::test::CheckNamed(failures, "either Shift requests fast walk", poll().fast_walk);
        const pt::InputState held = poll();
        pt::test::CheckNamed(failures, "holding Shift keeps the boost without zoom or interaction", held.fast_walk && held.held == 0);
        device.InjectKey(key, false);
        pt::test::CheckNamed(failures, "releasing Shift clears the boost", !poll().fast_walk);
    }
    device.InjectKey(SDL_SCANCODE_LSHIFT, true);
    device.InjectKey(SDL_SCANCODE_RSHIFT, true);
    poll();
    device.InjectKey(SDL_SCANCODE_LSHIFT, false);
    pt::test::CheckNamed(failures, "one Shift remains held after releasing the other", poll().fast_walk);
    device.InjectKey(SDL_SCANCODE_RSHIFT, false);
    pt::test::CheckNamed(failures, "releasing both Shift keys clears the boost", !poll().fast_walk);
    for (SDL_Scancode key : {SDL_SCANCODE_E, SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE}) {
        device.InjectKey(key, true);
        const pt::InputState input = poll();
        pt::test::CheckNamed(failures, "keyboard interaction works without requesting fast walk", !input.fast_walk && (input.held & pt::kPadAction));
        device.InjectKey(key, false);
        poll();
    }
    device.InjectMouseButton(SDL_BUTTON_LEFT, true);
    const pt::InputState mouse = poll();
    pt::test::CheckNamed(failures, "mouse interaction works without requesting fast walk", !mouse.fast_walk && (mouse.held & pt::kPadAction));
    device.InjectMouseButton(SDL_BUTTON_LEFT, false);
    poll();
    for (const char* kind : {"ps5", "xbox", "switch"}) {
        pt::test::CheckNamed(failures, std::string(kind) + " pad attaches", pads.Attach(0, kind));
        poll();
        pads.SetButton(0, "cross", true);
        const pt::InputState pressed = poll();
        pt::test::CheckNamed(failures, std::string(kind) + " bottom button boosts and still interacts",
              pressed.fast_walk && (pressed.held & pt::kPadAction) && (pressed.pressed & pt::kPadAction));
        const pt::InputState held = poll();
        pt::test::CheckNamed(failures, "a held controller button boosts without repeating the interaction edge", held.fast_walk && !(held.pressed & pt::kPadAction));
        pt::test::CheckNamed(failures, "captured controller input does not boost", !poll(false).fast_walk);
        pads.SetButton(0, "cross", false);
        pt::test::CheckNamed(failures, "released controller button clears the boost", !poll().fast_walk);
        for (const char* face : {"circle", "square", "triangle"}) {
            pads.SetButton(0, face, true);
            pt::test::CheckNamed(failures, "other face buttons do not request fast walk", !poll().fast_walk);
            pads.SetButton(0, face, false);
            poll();
        }
        // Prompt selection remembers the pad; it must not turn a later keyboard action into a boost.
        device.InjectKey(SDL_SCANCODE_E, true);
        pt::test::CheckNamed(failures, "keyboard interaction after pad use does not boost", !poll().fast_walk);
        device.InjectKey(SDL_SCANCODE_E, false);
        pads.SetButton(0, "cross", true);
        poll();
        pads.Detach(0);
        pt::test::CheckNamed(failures, "disconnecting a held controller clears the boost", !poll().fast_walk);
    }
    device.Shutdown();
}

void Tick(pt::game::Player& player, const pt::InputState& input, const pt::game::PlayerFrameContext& context, int ticks = 4) {
    const pt::CollisionWorld world;
    for (int tick = 0; tick < ticks; ++tick) {
        player.Update(1.0f / 60.0f, input, world, context);
        player.EndFrame();
    }
}

float Rate(glm::vec2 stick, bool fast, bool red_loop = false) {
    pt::game::Player player;
    player.Warp(glm::vec3(0.0f), 0.0f);
    pt::InputState input;
    input.left_stick = stick;
    pt::game::PlayerFrameContext context;
    context.fast_walk = fast;
    context.full_screen_blur = red_loop;
    Tick(player, input, context);
    return player.MovementRate();
}

void CheckRatesAndTiming() {
    using namespace pt;
    using namespace pt::game;
    pt::test::CheckNamed(failures, "forward boost is 50 percent", Near(Rate({0, 1}, true), 1.5f));
    pt::test::CheckNamed(failures, "backward keeps its lower original speed", Near(Rate({0, -1}, true), 0.75f));
    pt::test::CheckNamed(failures, "red loop preserves its original multiplier", Near(Rate({0, 1}, true, true), 5.25f));
    for (const glm::vec2 stick : {glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0.5f, 0.5f), glm::vec2(0, 0.35f)}) {
        pt::test::CheckNamed(failures, "direction and analog strength retain their original ratio", Near(Rate(stick, true), 1.5f * Rate(stick, false)));
    }
    pt::test::CheckNamed(failures, "idle does not accelerate", Near(Rate({0, 0}, true), 1.0f));
    Player player;
    player.Warp(glm::vec3(0.0f), 0.0f);
    InputState input;
    input.left_stick = {0, 1};
    PlayerFrameContext context;
    context.fast_walk = true;
    Tick(player, input, context);
    context.fast_walk = false;
    Tick(player, input, context);
    pt::test::CheckNamed(failures, "release restores the original movement rate", Near(player.MovementRate(), 1.0f));
    context.fast_walk = true;
    player.SetDisableLeftStick(true);
    Tick(player, input, context);
    pt::test::CheckNamed(failures, "a cutscene movement lock prevents acceleration", Near(player.MovementRate(), 1.0f) && player.Standing());

    Player normal, fast;
    normal.Warp(glm::vec3(0.0f), 0.0f);
    fast.Warp(glm::vec3(0.0f), 0.0f);
    input.right_stick = {0.25f, -0.2f};
    input.from_gamepad = true;
    input.held = kPadZoom | kPadAction;
    PlayerFrameContext normal_context, fast_context;
    fast_context.fast_walk = true;
    const CollisionWorld world;
    bool same_cadence = true, same_look = true, same_buttons = true;
    for (int tick = 0; tick < 600; ++tick) {
        normal.Update(1.0f / 60.0f, input, world, normal_context);
        fast.Update(1.0f / 60.0f, input, world, fast_context);
        same_cadence &= normal.FrameEnds() == fast.FrameEnds();
        same_look &= Near(normal.yaw, fast.yaw) && Near(normal.pitch, fast.pitch) && Near(normal.zoom, fast.zoom);
        same_buttons &= normal.HeldButtons() == fast.HeldButtons();
        normal.EndFrame();
        fast.EndFrame();
    }
    pt::test::CheckNamed(failures, "original frame cadence is unchanged", same_cadence);
    pt::test::CheckNamed(failures, "camera turning and zoom are unchanged", same_look);
    pt::test::CheckNamed(failures, "player interaction and zoom buttons are unchanged", same_buttons);
}

void CheckTravel(const char* folder) {
    pt::Vfs vfs;
    const bool loaded = vfs.Mount(folder) && vfs.LoadPackage("/Assets/sh/level/common/resident.fpk") &&
                        pt::game::LoadPlayerAnimation(vfs) && pt::game::PlayerLocomotionData().loaded;
    pt::test::CheckNamed(failures, "original walking animations load", loaded);
    if (!loaded) return;
    pt::game::Player normal, fast;
    normal.Warp(glm::vec3(0.0f), 0.0f);
    fast.Warp(glm::vec3(0.0f), 0.0f);
    pt::InputState input;
    input.left_stick = {0, 1};
    pt::game::PlayerFrameContext context;
    Tick(normal, input, context, 600);
    context.fast_walk = true;
    Tick(fast, input, context, 600);
    const float normal_distance = glm::length(glm::vec2(normal.Feet().x, normal.Feet().z));
    const float fast_distance = glm::length(glm::vec2(fast.Feet().x, fast.Feet().z));
    std::printf("ten-second travel: normal %.3f m, fast %.3f m (%.3fx)\n", normal_distance, fast_distance, fast_distance / normal_distance);
    pt::test::CheckNamed(failures, "animation-driven travel is approximately 1.5x", normal_distance > 1.0f && Near(fast_distance / normal_distance, 1.5f, 0.03f));
    pt::test::CheckNamed(failures, "gravity advances at the original rate", Near(normal.Feet().y, fast.Feet().y));
    std::array<pt::GeomTriangle, 4> floor_and_wall{};
    floor_and_wall[0].a = {-10, -2, 2}; floor_and_wall[0].b = {-10, 5, 2}; floor_and_wall[0].c = {10, 5, 2};
    floor_and_wall[1].a = {-10, -2, 2}; floor_and_wall[1].b = {10, 5, 2}; floor_and_wall[1].c = {10, -2, 2};
    floor_and_wall[2].a = {-20, 0, -20}; floor_and_wall[2].b = {-20, 0, 20}; floor_and_wall[2].c = {20, 0, 20};
    floor_and_wall[3].a = {-20, 0, -20}; floor_and_wall[3].b = {20, 0, 20}; floor_and_wall[3].c = {20, 0, -20};
    pt::CollisionWorld world;
    world.AddTriangles(floor_and_wall, glm::mat4(1.0f), -1);
    world.Build();
    fast.Warp(glm::vec3(0.0f), 0.0f);
    bool stayed_outside = true;
    for (int tick = 0; tick < 600; ++tick) {
        fast.Update(1.0f / 60.0f, input, world, context);
        stayed_outside &= fast.Feet().z < 1.8f;
        fast.EndFrame();
    }
    pt::test::CheckNamed(failures, "boosted walking stops at a wall", stayed_outside && fast.Feet().z > 1.0f);
    pt::test::CheckNamed(failures, "boosted walking stays on the floor", std::abs(fast.Feet().y) < 0.1f);
}

}

int main(int argc, char** argv) {
    if (argc > 2) return 2;
    pt::VirtualPads::UseOnlyVirtualDevices();
    if (!SDL_Init(SDL_INIT_GAMEPAD)) return 2;
    CheckInput();
    if (argc == 2) CheckTravel(argv[1]);
    CheckRatesAndTiming();
    SDL_Quit();
    return failures ? 1 : 0;
}
