#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

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

struct Noise {
    uint32_t state = 12345;
    float Next(float amplitude) {
        state = state * 1664525u + 1013904223u;
        return (static_cast<float>(state >> 8) / 8388608.0f - 1.0f) * amplitude;
    }
    glm::vec3 Vec(float amplitude) { return {Next(amplitude), Next(amplitude), Next(amplitude)}; }
};

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

    {
        const glm::vec3 bias(0.20f, -0.12f, 0.07f);
        const float dt = 1.0f / 200.0f;
        Noise noise;
        pt::GyroTracker gyro;
        float first_second = 0.0f, settled = 0.0f;
        for (int second = 0; second < 12; ++second) {
            for (int i = 0; i < 200; ++i) gyro.Add(bias + noise.Vec(0.008f), dt);
            const glm::vec2 turn = gyro.TakeTurn();
            if (second == 0) first_second = glm::length(turn);
            if (second >= 2) settled += glm::length(turn);
        }
        std::printf("rest with a 0.20 rad/s offset: %.4f rad in the first second, %.5f rad over seconds 3 to 12\n", first_second, settled);
        Check(first_second < 0.08f, "a resting pad with an offset turns little even in its first second");
        Check(settled < 0.002f, "from the third second a resting pad with an offset hands on no camera motion");
        Check(Near(gyro.Offset().x, bias.x, 0.004f) && Near(gyro.Offset().y, bias.y, 0.004f) && Near(gyro.Offset().z, bias.z, 0.004f),
              "the offset is learned from the resting pad");
        Check(gyro.Still() && gyro.Learned(), "the resting pad counts as still");
        for (int i = 0; i < 200; ++i) gyro.Add(bias + glm::vec3(0.0f, 1.0f, 0.0f) + noise.Vec(0.008f), dt);
        const glm::vec2 moved = gyro.TakeTurn();
        Check(Near(moved.x, 1.0f, 0.03f) && Near(moved.y, 0.0f, 0.03f), "a real turn still passes through at the right scale");
        for (int i = 0; i < 200; ++i) gyro.Add(bias + noise.Vec(0.008f), dt);
        gyro.TakeTurn();
        for (int i = 0; i < 400; ++i) gyro.Add(bias + noise.Vec(0.008f), dt);
        Check(glm::length(gyro.TakeTurn()) < 0.002f, "after the turn the pad rests and hands on nothing");
        pt::GyroTracker aim;
        for (int i = 0; i < 2400; ++i) aim.Add(bias + noise.Vec(0.008f), dt, 3.0f);
        Check(glm::length(aim.Aim()) < 0.002f, "a resting pad with an offset keeps the flashlight's aim in the middle");
        const glm::vec3 warm = bias + glm::vec3(0.05f, 0.03f, 0.0f);
        for (int i = 0; i < 600; ++i) gyro.Add(warm + noise.Vec(0.008f), dt);
        gyro.TakeTurn();
        Check(Near(gyro.Offset().x, warm.x, 0.004f), "an offset that drifts is followed");

        pt::GyroTracker stored;
        stored.SetOffset(bias);
        for (int i = 0; i < 100; ++i) stored.Add(bias + noise.Vec(0.008f), dt);
        Check(glm::length(stored.TakeTurn()) < 0.001f, "a stored offset needs no learning time");

        pt::GyroTracker cal;
        cal.StartCalibration();
        for (int i = 0; i < 300; ++i) cal.Add(bias + noise.Vec(0.008f), dt);
        Check(cal.CalibrationState() == pt::GyroTracker::Calibration::Running && Near(cal.CalibrationProgress(), 0.5f, 0.02f), "calibration reports its progress");
        for (int i = 0; i < 320; ++i) cal.Add(bias + noise.Vec(0.008f), dt);
        glm::vec3 result;
        Check(cal.CalibrationState() == pt::GyroTracker::Calibration::Done && cal.TakeCalibrated(result) && !cal.TakeCalibrated(result),
              "three seconds of rest finish the calibration, once");
        Check(Near(result.x, bias.x, 0.003f) && Near(result.y, bias.y, 0.003f) && Near(result.z, bias.z, 0.003f), "the calibrated offset is the resting offset");
        pt::GyroTracker touched;
        touched.StartCalibration();
        for (int i = 0; i < 200; ++i) touched.Add(bias + noise.Vec(0.008f), dt);
        for (int i = 0; i < 20; ++i) touched.Add(bias + glm::vec3(0.0f, 0.6f, 0.0f), dt);
        Check(touched.CalibrationState() == pt::GyroTracker::Calibration::Failed && !touched.TakeCalibrated(result),
              "a controller that is moved during the calibration fails it");

        pt::GyroTracker tight, loose;
        tight.SetOffset(glm::vec3(0.0f));
        loose.SetOffset(glm::vec3(0.0f));
        loose.SetTightening(0.0f);
        for (int i = 0; i < 40; ++i) {
            tight.Add({0.0f, 0.06f, 0.0f}, dt);
            loose.Add({0.0f, 0.06f, 0.0f}, dt);
        }
        const float ratio = tight.TakeTurn().x / loose.TakeTurn().x;
        Check(Near(ratio, 0.06f / pt::GyroTracker::kDefaultTightening, 0.01f), "the tightening scales a turn below its threshold by its share");
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
    run(120);
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
    pads.SetGyro(0, 0.0f, 0.15f, 0.0f);
    run(120);
    poll();
    run(60);
    state = poll();
    Check(state.gyro_turn == glm::vec2(0.0f), "a pad with a 0.15 rad/s offset at rest turns nothing after a moment");
    input.StartGyroCalibration();
    run(200);
    std::string guid;
    std::array<float, 3> offset{};
    Check(input.GyroCalibration() == pt::GyroTracker::Calibration::Done && input.TakeGyroCalibrated(guid, offset) && !guid.empty() &&
              Near(offset[1], 0.15f, 0.002f) && Near(offset[0], 0.0f, 0.002f),
          "the calibration through the input device hands on the offset with the controller's GUID");
    input.ClearGyroCalibration();
    Check(input.GyroCalibration() == pt::GyroTracker::Calibration::None, "a finished calibration is cleared for the settings row");
    pads.Detach(0);
    poll();
    input.settings.gyro_bias[guid] = offset;
    Check(pads.Attach(0, "ps5"), "the virtual DualSense attaches again");
    poll();
    pads.SetGyro(0, 0.0f, 0.15f, 0.0f);
    run(3);
    poll();
    run(6);
    state = poll();
    Check(glm::length(state.gyro_turn) < 1.0e-4f && Near(input.Gyro().Offset().y, 0.15f, 0.002f), "a calibrated controller starts from its stored offset");
    pads.SetGyro(0, 0.0f, 0.0f, 0.0f);
    run(120);
    poll();

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
