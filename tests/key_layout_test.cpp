#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/platform/input.h"

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdio>
#include <string>

using pt::FirstBinding;
using pt::KeyAction;
using pt::KeyBindingName;
using pt::SetKeyLayoutForTest;

namespace {

uint32_t Qwerty(uint16_t scancode) {
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) return 'a' + (scancode - SDL_SCANCODE_A);
    return SDLK_UNKNOWN;
}

uint32_t Azerty(uint16_t scancode) {
    switch (scancode) {
        case SDL_SCANCODE_A: return 'q';
        case SDL_SCANCODE_Q: return 'a';
        case SDL_SCANCODE_W: return 'z';
        case SDL_SCANCODE_Z: return 'w';
        default: return Qwerty(scancode);
    }
}

uint32_t Qwertz(uint16_t scancode) {
    switch (scancode) {
        case SDL_SCANCODE_Y: return 'z';
        case SDL_SCANCODE_Z: return 'y';
        default: return Qwerty(scancode);
    }
}

uint32_t TurkishF(uint16_t scancode) {
    switch (scancode) {
        case SDL_SCANCODE_Q: return 'f';
        case SDL_SCANCODE_W: return 'g';
        case SDL_SCANCODE_E: return 0x011F;
        case SDL_SCANCODE_A: return 'u';
        case SDL_SCANCODE_S: return 'i';
        case SDL_SCANCODE_D: return 'e';
        case SDL_SCANCODE_H: return 'k';
        default: return Qwerty(scancode);
    }
}

uint32_t Russian(uint16_t scancode) {
    switch (scancode) {
        case SDL_SCANCODE_Q: return 0x0439;
        case SDL_SCANCODE_E: return 0x0443;
        case SDL_SCANCODE_P: return 0x0437;
        case SDL_SCANCODE_W: return 0x0446;
        case SDL_SCANCODE_S: return 0x044B;
        default: return SDLK_UNKNOWN;
    }
}
uint32_t Ukrainian(uint16_t scancode) { return scancode == SDL_SCANCODE_S ? 0x0456 : Russian(scancode); }
uint32_t Greek(uint16_t scancode) {
    switch (scancode) {
        case SDL_SCANCODE_Q: return ';';
        case SDL_SCANCODE_E: return 0x03B5;
        case SDL_SCANCODE_P: return 0x03C0;
        case SDL_SCANCODE_W: return 0x03C2;
        case SDL_SCANCODE_S: return 0x03C3;
        default: return SDLK_UNKNOWN;
    }
}
uint32_t Arabic(uint16_t scancode) {
    switch (scancode) {
        case SDL_SCANCODE_Q: return 0x0636;
        case SDL_SCANCODE_E: return 0x062B;
        case SDL_SCANCODE_P: return 0x062D;
        case SDL_SCANCODE_A: return 0x0634;
        default: return SDLK_UNKNOWN;
    }
}
uint32_t Hebrew(uint16_t scancode) {
    switch (scancode) {
        case SDL_SCANCODE_Q: return '/';
        case SDL_SCANCODE_W: return '\'';
        case SDL_SCANCODE_E: return 0x05E7;
        case SDL_SCANCODE_P: return 0x05E4;
        case SDL_SCANCODE_A: return 0x05E9;
        default: return SDLK_UNKNOWN;
    }
}

std::string Name(KeyAction action) {
    const pt::KeyBinding* binding = FirstBinding(action, true);
    assert(binding);
    return KeyBindingName(*binding);
}

}

int main() {
    SetKeyLayoutForTest(Qwerty);
    assert(Name(KeyAction::DateAuto) == "A");
    assert(Name(KeyAction::WalkForward) == "W");
    assert(Name(KeyAction::MenuHouse) == "H");
    assert(Name(KeyAction::DateSkip) == "Tab");
    assert(Name(KeyAction::Confirm) == "Enter");
    assert(Name(KeyAction::Menu) == "Esc");

    SetKeyLayoutForTest(Azerty);
    assert(Name(KeyAction::DateAuto) == "Q");
    assert(Name(KeyAction::WalkForward) == "Z");
    assert(Name(KeyAction::MenuQuit) == "A");
    assert(Name(KeyAction::DateSkip) == "Tab");

    SetKeyLayoutForTest(Qwertz);
    assert(Name(KeyAction::DateAuto) == "A");
    assert(Name(KeyAction::WalkForward) == "W");

    SetKeyLayoutForTest(TurkishF);
    assert(Name(KeyAction::DateAuto) == "U");
    assert(Name(KeyAction::WalkForward) == "G");
    assert(Name(KeyAction::MenuHouse) == "K");
    assert(Name(KeyAction::Confirm) == "Enter");
    const pt::KeyBinding gbreve{KeyAction::Act, static_cast<uint16_t>(SDL_SCANCODE_E), 0};
    const std::string g = KeyBindingName(gbreve);
    std::printf("Turkish F E key: %s\n", g.c_str());
    assert(g == "\xC4\x9E" || g == "\xC4\x9F");

    SetKeyLayoutForTest(nullptr);
    std::printf("key layout test: ok\n");
    return 0;
}
