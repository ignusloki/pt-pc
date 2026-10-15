#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#include "engine/core/log.h"
#include "engine/platform/boot_timing.h"
#include "engine/platform/device_start.h"
#include "engine/platform/input.h"
#include "engine/platform/virtual_pad.h"

namespace {

int failures = 0;

void Check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    failures += ok ? 0 : 1;
}

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

using Clock = std::chrono::steady_clock;

double Seconds(Clock::time_point since) {
    return std::chrono::duration<double>(Clock::now() - since).count();
}

double TimedPump() {
    const auto start = Clock::now();
    SDL_PumpEvents();
    return Seconds(start);
}

}

int main() {
    Check(pt::boot::PhaseText("game folder", 0.0123, "") == "game folder 0.012 s", "a boot line without a note");
    Check(pt::boot::PhaseText("controllers", 5.4, "on its own thread, 1 gamepad connected") ==
              "controllers 5.400 s (on its own thread, 1 gamepad connected)",
          "a boot line with a note");
    Check(pt::boot::SinceProcessStart() > 0.0, "the process's age is known");

    const std::filesystem::path log = std::filesystem::temp_directory_path() / "pt_device_start_test.log";
    pt::LogSetFile(log);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    pt::VirtualPads::UseOnlyVirtualDevices();
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "1");
    SetEnv("PT_TEST_SLOW_GAMEPAD_MS", "1500");
    SetEnv("PT_TEST_SLOW_AUDIO_MS", "800");
    SetEnv("PT_TEST_SLOW_DIRECTINPUT_MS", "2600");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("FAIL SDL_Init(VIDEO): %s\n", SDL_GetError());
        return 2;
    }
#ifdef _WIN32
    const bool background = true;
#else
    const bool background = false;
#endif
    const auto started = Clock::now();
    pt::devices::Start({.gamepad = true, .audio = true, .background = background});
    const double start_call = Seconds(started);
    if (background) {
        Check(start_call < 0.1, std::format("Start returns at once ({:.3f} s) with both starts slow", start_call));
        Check(pt::devices::AudioStarting(), "the audio says it is starting");
        Check(!pt::devices::EnsureAudio(), "no SDL audio call while it starts");
        Check(!pt::devices::GamepadsReady() && !pt::devices::TakeGamepadsReady(), "the pads are not handed over while they start");
    }

    pt::InputDevice input;
    double longest_pump = 0.0;
    bool audio_over_first = false;
    bool handed = !background;
    int handovers = 0;
    while (!handed && Seconds(started) < 20.0) {
        longest_pump = std::max(longest_pump, TimedPump());
        SDL_Event event;
        while (SDL_PollEvent(&event)) input.ProcessEvent(event);
        if (!pt::devices::AudioStarting() && !pt::devices::GamepadsReady()) audio_over_first = true;
        if (pt::devices::TakeGamepadsReady()) {
            ++handovers;
            handed = true;
            input.Init();
        }
        SDL_Delay(5);
    }
    const double handed_at = Seconds(started);
    if (background) {
        Check(handed, std::format("the pads are handed over ({:.2f} s after the start)", handed_at));
        Check(handed_at > 3.5, "after the held DirectInput budget (2 s) and the held controller start (1.5 s)");
        Check(longest_pump < 0.1, std::format("the main thread's event pump never waited (longest {:.3f} s)", longest_pump));
        Check(audio_over_first, "the audio start (0.8 s) was over before the controllers'");
        Check(!pt::devices::TakeGamepadsReady() && handovers == 1, "the hand-over happens once");
    }
    Check(pt::devices::WaitAudio(5.0) && !pt::devices::AudioStarting(), "the audio start is over");
    Check(pt::devices::EnsureAudio(), "the audio subsystem is usable");
    const char* driver = SDL_GetCurrentAudioDriver();
    Check(driver && std::string(driver) == "dummy", "the dummy audio driver runs");
    Check(pt::devices::GamepadsReady(), "the pads may be used");

    pt::VirtualPads pads;
    Check(pads.Attach(0, "ps5"), "a virtual DualSense attaches after the start");
    for (int i = 0; i < 20 && input.GamepadCount() == 0; ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) input.ProcessEvent(event);
        SDL_Delay(5);
    }
    Check(input.GamepadCount() == 1, "the input device opened it");
    pads.Detach(0);
    for (int i = 0; i < 20 && input.GamepadCount() != 0; ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) input.ProcessEvent(event);
        SDL_Delay(5);
    }
    Check(input.GamepadCount() == 0, "and closed it when it was unplugged");

    const auto stop_started = Clock::now();
    const bool clean = pt::devices::Stop();
    Check(clean, std::format("the controller thread ends at Stop ({:.3f} s)", Seconds(stop_started)));
    if (clean) SDL_Quit();

    pt::LogSetFile({});
    std::ifstream in(log);
    std::stringstream text;
    text << in.rdbuf();
    const std::string lines = text.str();
    Check(lines.find("boot: controllers ") != std::string::npos, "the controllers' start time is in the log");
    Check(lines.find("boot: audio devices ") != std::string::npos, "the audio start time is in the log");
    if (background) {
        Check(lines.find("DirectInput enumeration not over after 2 s") != std::string::npos,
              "a DirectInput enumeration past its budget is logged and left out");
        Check(lines.find("the controllers took") == std::string::npos, "the held start stays under the slow start warning (2 s)");
    }
    std::printf("%s\n", failures ? "device start test FAILED" : "device start test passed");
    return failures ? 1 : 0;
}
