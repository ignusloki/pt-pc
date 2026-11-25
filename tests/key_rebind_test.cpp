#include <SDL3/SDL_scancode.h>
#include <cstdio>
#include <map>
#include <string>

#include "engine/platform/input.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++g_failures;
    }
}

}

int main() {
    using namespace pt;
    ResetKeyBindings();
    Check(ExportKeyBindings().empty(), "defaults export empty overrides");

    Check(SetKeyBinding(KeyAction::SurvivalReload, SDL_SCANCODE_F, 0), "rebind SurvivalReload to F");
    const KeyBinding* reload = FirstBinding(KeyAction::SurvivalReload);
    Check(reload && reload->scancode == SDL_SCANCODE_F && reload->mouse == 0, "SurvivalReload updated to F");

    Check(SetKeyBinding(KeyAction::SurvivalFlare, SDL_SCANCODE_F, 0), "rebind SurvivalFlare to F swaps with Reload");
    const KeyBinding* flare = FirstBinding(KeyAction::SurvivalFlare);
    reload = FirstBinding(KeyAction::SurvivalReload);
    Check(flare && flare->scancode == SDL_SCANCODE_F, "SurvivalFlare is now F");
    Check(reload && reload->scancode == SDL_SCANCODE_G, "SurvivalReload swapped cleanly to G");

    Check(!SetKeyBinding(KeyAction::WalkForward, SDL_SCANCODE_ESCAPE, 0), "Escape rejected as rebind target");

    const std::map<std::string, std::string> exported = ExportKeyBindings();
    Check(exported.size() == 2, "two non-default bindings exported");
    ResetKeyBindings();
    Check(FirstBinding(KeyAction::SurvivalReload)->scancode == SDL_SCANCODE_R, "ResetKeyBindings restores R");
    ApplyKeyBindings(exported);
    Check(FirstBinding(KeyAction::SurvivalFlare)->scancode == SDL_SCANCODE_F, "ApplyKeyBindings restores Flare=F");
    Check(FirstBinding(KeyAction::SurvivalReload)->scancode == SDL_SCANCODE_G, "ApplyKeyBindings restores Reload=G");

    Check(SetKeyBinding(KeyAction::Zoom, 0, 2), "rebind Zoom to middle mouse");
    const auto exported2 = ExportKeyBindings();
    ResetKeyBindings();
    ApplyKeyBindings(exported2);
    Check(FirstBinding(KeyAction::Zoom)->mouse == 2 && FirstBinding(KeyAction::Zoom)->scancode == 0, "Zoom middle mouse roundtrip");

    InputDevice device;
    device.BeginKeyCapture();
    device.InjectKey(SDL_SCANCODE_UP, true);
    uint16_t captured = 0;
    uint8_t mouse = 0;
    bool cancelled = false;
    Check(device.PollCapturedKey(captured, mouse, cancelled) && !cancelled && captured == SDL_SCANCODE_UP, "capture Up");
    Check(SetKeyBinding(KeyAction::WalkForward, captured, mouse), "bind forward movement to Up");
    Check(device.Poll(false, MouseUse::Look).left_stick.y == 0.0f, "captured Up waits for release");
    device.InjectKey(SDL_SCANCODE_UP, false);
    device.Poll(false, MouseUse::Look);
    device.InjectKey(SDL_SCANCODE_UP, true);
    device.InjectKey(SDL_SCANCODE_LSHIFT, true);
    const InputState walking = device.Poll(false, MouseUse::Look);
    Check(walking.left_stick.y == 1.0f && walking.fast_walk && !(walking.raw_held & kRawUp), "Shift and rebound Up fast-walk without moving the camera");
    Check(device.Poll(false, MouseUse::Menu).raw_held & kRawUp, "Up still navigates menus");

    ResetKeyBindings();
    std::printf("%d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
