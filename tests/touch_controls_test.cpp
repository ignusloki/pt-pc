#include "engine/platform/touch_controls.h"
#include "engine/platform/virtual_pad.h"
#include <cassert>

int main() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    pt::VirtualPads::UseOnlyVirtualDevices();
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
    auto* window = SDL_CreateWindow("touch test", 1000, 500, SDL_WINDOW_HIDDEN);
    assert(window);
    {
        pt::VirtualPads pads;
        pt::TouchControls touch;
        touch.Attach(pads, 3);
        auto* joystick = SDL_GetJoystickFromID(pads.Id(3));
        assert(joystick);
        const auto finger = [&](Uint32 type, float x, float y) {
            SDL_Event event{};
            event.type = type; event.tfinger.fingerID = 42;
            event.tfinger.x = x; event.tfinger.y = y;
            touch.ProcessEvent(event, window);
            SDL_UpdateJoysticks();
        };
        finger(SDL_EVENT_FINGER_DOWN, .1f, .5f);
        finger(SDL_EVENT_FINGER_MOTION, .2f, .5f);
        assert(SDL_GetJoystickAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX) > 10000);
        // Interrupted touches must stop movement even without a finger-up event.
        SDL_Event event{}; event.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
        touch.ProcessEvent(event, window); SDL_UpdateJoysticks();
        assert(SDL_GetJoystickAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX) == 0);
        finger(SDL_EVENT_FINGER_DOWN, .1f, .5f);
        finger(SDL_EVENT_FINGER_MOTION, .2f, .5f);
        touch.SetMenuOpen(true); SDL_UpdateJoysticks();
        assert(SDL_GetJoystickAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX) == 0);
        finger(SDL_EVENT_FINGER_DOWN, .1f, .5f);
        finger(SDL_EVENT_FINGER_MOTION, .1f, .7f);
        assert(SDL_GetJoystickButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        finger(SDL_EVENT_FINGER_CANCELED, .1f, .7f);
        assert(!SDL_GetJoystickButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        finger(SDL_EVENT_FINGER_DOWN, .2f, .1f);
        assert(touch.TakeColumn());
        assert(!touch.TakeColumn());
        finger(SDL_EVENT_FINGER_CANCELED, .2f, .1f);
        touch.SetMenuOpen(false);
        finger(SDL_EVENT_FINGER_DOWN, .1f, .5f);
        finger(SDL_EVENT_FINGER_MOTION, .2f, .5f);
        event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        event.gbutton.which = 0xffffffffu; // non-virtual hardware event
        touch.ProcessEvent(event, window); SDL_UpdateJoysticks();
        assert(SDL_GetJoystickAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX) == 0);
        finger(SDL_EVENT_FINGER_DOWN, .7f, .5f);
        finger(SDL_EVENT_FINGER_MOTION, .75f, .5f);
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        touch.ProcessEvent(event, window);
        assert(touch.TakeLook().x == 0);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
}
