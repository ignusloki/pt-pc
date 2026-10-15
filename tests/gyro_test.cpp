#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

#include "engine/platform/gyro.h"
#include "engine/platform/input.h"
#include "engine/platform/virtual_pad.h"

namespace {

int failures = 0;

void Check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    failures += ok ? 0 : 1;
}

bool Near(float a, float b, float tolerance = 0.01f) { return std::abs(a - b) <= tolerance; }

}

int main() {
    {
        pt::GyroTracker gyro;
        for (int i = 0; i < 60; ++i) gyro.Add({0.0f, 1.0f, 0.0f}, 1.0f / 60.0f);
        glm::vec2 turn = gyro.TakeTurn();
        Check(Near(turn.x, 1.0f) && Near(turn.y, 0.0f), "a turn left of 1 rad/s for a second is a radian left");
        for (int i = 0; i < 60; ++i) gyro.Add({0.5f, 0.0f, 0.0f}, 1.0f / 60.0f);
        turn = gyro.TakeTurn();
        Check(Near(turn.y, 0.5f) && Near(turn.x, 0.0f), "pitching up is a positive y");
        Check(gyro.TakeTurn() == glm::vec2(0.0f), "the turn is taken once");

        pt::GyroTracker still;
        for (int i = 0; i < 120; ++i) still.Add({0.02f, 0.03f, -0.01f}, 1.0f / 60.0f);
        Check(still.TakeTurn().x < 0.001f && still.Still(), "a pad lying still with a small rate offset turns nothing");
        Check(Near(still.Offset().y, 0.03f, 0.005f), "the resting offset is learned");
        for (int i = 0; i < 60; ++i) still.Add({0.02f, 1.03f, -0.01f}, 1.0f / 60.0f);
        Check(Near(still.TakeTurn().x, 1.0f, 0.03f), "the learned offset is taken off a real turn");

        pt::GyroTracker aim;
        for (int i = 0; i < 300; ++i) aim.Add({0.0f, 2.0f, 0.0f}, 1.0f / 60.0f);
        Check(Near(aim.Aim().x, pt::GyroTracker::kAimLimit.x, 1.0e-4f), "the aim stops at the flashlight's range");
        Check(Near(aim.AimStick().x, -1.0f, 1.0e-3f) && Near(aim.AimStick().y, 0.0f), "a full left is the stick's full left");
        for (int i = 0; i < 300; ++i) aim.Add({0.0f, 0.0f, 0.0f}, 1.0f / 60.0f);
        Check(std::abs(aim.Aim().x) < 0.01f, "the aim comes back to the middle once the pad is still");
        for (int i = 0; i < 20; ++i) aim.Add({-1.0f, 0.0f, 0.0f}, 1.0f / 60.0f);
        Check(aim.AimStick().y > 0.2f, "pitching down aims the light down (the stick's down is positive)");
        aim.Recenter();
        Check(aim.Aim() == glm::vec2(0.0f), "recentring puts the aim in the middle");
        pt::GyroTracker scaled;
        for (int i = 0; i < 30; ++i) scaled.Add({0.0f, 0.5f, 0.0f}, 1.0f / 60.0f, 2.0f);
        Check(Near(scaled.Aim().x, 0.5f, 0.01f) && Near(scaled.TakeTurn().x, 0.25f, 0.01f), "the sensitivity scales the aim, not the turn");
    }

    pt::VirtualPads::UseOnlyVirtualDevices();
    if (!SDL_Init(SDL_INIT_GAMEPAD)) return 2;
    pt::InputDevice input;
    input.settings.rumble = false;
    input.Init();
    pt::VirtualPads pads;
    auto poll = [&] {
        SDL_UpdateGamepads();
        SDL_Event event;
        while (SDL_PollEvent(&event)) input.ProcessEvent(event);
        return input.Poll(false, pt::MouseUse::Look);
    };
    auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            pads.PumpSensors(1.0f / 60.0f);
            SDL_UpdateGamepads();
            SDL_Event event;
            while (SDL_PollEvent(&event)) input.ProcessEvent(event);
        }
    };
    Check(pads.Attach(0, "ps5"), "a virtual DualSense attaches");
    poll();

    input.settings.gyro = 0;
    poll();
    pads.SetGyro(0, 0.0f, 1.0f, 0.0f);
    run(60);
    pt::InputState state = poll();
    Check(state.gyro_turn == glm::vec2(0.0f) && state.gyro_light == glm::vec2(0.0f), "gyro off hands nothing on");

    input.settings.gyro = 1;
    poll();
    run(2);
    poll();
    run(60);
    state = poll();
    Check(Near(state.gyro_turn.x, 1.0f, 0.05f) && Near(state.gyro_turn.y, 0.0f, 0.01f), "camera mode: a second at 1 rad/s turns a radian left");
    Check(state.gyro_light == glm::vec2(0.0f), "camera mode leaves the light alone");
    input.settings.gyro_sensitivity = 2.0f;
    run(60);
    state = poll();
    Check(Near(state.gyro_turn.x, 2.0f, 0.1f), "the sensitivity doubles the turn");
    input.settings.gyro_sensitivity = 1.0f;
    pads.SetGyro(0, 0.0f, 0.0f, 0.0f);
    run(60);
    poll();
    state = poll();
    Check(state.gyro_turn == glm::vec2(0.0f), "a pad at rest turns nothing");

    input.settings.gyro = 2;
    poll();
    pads.SetGyro(0, 0.0f, -0.5f, 0.0f);
    run(30);
    state = poll();
    Check(state.gyro_turn == glm::vec2(0.0f) && state.gyro_light.x > 0.2f && state.gyro_light.y == 0.0f,
          "flashlight mode: turning right aims the light right (positive x), not the view");
    pads.SetGyro(0, 0.0f, 0.0f, 0.0f);
    run(10);
    state = poll();
    Check(state.gyro_light.x > 0.1f, "the offset is still there just after the pad stops");
    pads.SetButton(0, "l3", true);
    state = poll();
    Check(state.gyro_light == glm::vec2(0.0f), "L3 puts the light back in the middle");
    pads.SetButton(0, "l3", false);
    poll();
    pads.SetGyro(0, 0.0f, -0.5f, 0.0f);
    run(30);
    poll();
    pads.SetGyro(0, 0.0f, 0.0f, 0.0f);
    run(240);
    state = poll();
    Check(std::abs(state.gyro_light.x) < 0.02f, "the pad lying still brings the light back by itself");

    input.settings.gyro = 1;
    poll();
    pads.SetGyro(0, 0.0f, 1.0f, 0.0f);
    run(30);
    SDL_UpdateGamepads();
    state = input.Poll(false, pt::MouseUse::Menu, false);
    Check(state.gyro_turn == glm::vec2(0.0f), "the menus get no gyro");

    pads.Detach(0);
    poll();
    Check(pads.Attach(0, "xbox"), "a virtual Xbox pad attaches (no motion sensor)");
    poll();
    input.settings.gyro = 1;
    pads.SetGyro(0, 0.0f, 1.0f, 0.0f);
    poll();
    run(30);
    state = poll();
    Check(state.gyro_turn == glm::vec2(0.0f), "a pad without a motion sensor turns nothing");
    pads.DetachAll();
    std::printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
