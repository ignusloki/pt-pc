#pragma once

// On-screen touch controls for phones and tablets (docs/mobile.md): a virtual DualSense the game reads like any controller.
// Left half: a floating stick that walks; right half: a floating stick that looks; buttons for Cross, Circle, R1 (zoom),
// Options and "JACK" (L2 + R2: the word the game listens for, with the voice key setting on). Hidden while a real controller
// is in use; a touch brings them back. PT_TOUCH=1 turns them on elsewhere (the mouse acts as a finger), PT_TOUCH=0 off.

#include <SDL3/SDL.h>

#include <map>

struct ImDrawList;

namespace pt {

class VirtualPads;

class TouchControls {
public:
    static bool Wanted();  // touch platforms, or PT_TOUCH=1

    void Attach(VirtualPads& pads, int slot);
    void ProcessEvent(const SDL_Event& event, SDL_Window* window);
    void Draw(ImDrawList* list, float width, float height);

private:
    enum class Role { None, Button, Move, Look };
    struct Finger {
        Role role = Role::None;
        int button = -1;
        float x0 = 0, y0 = 0, x = 0, y = 0;  // pixels
    };

    void Begin(SDL_FingerID id, float x, float y);
    void Move(SDL_FingerID id, float x, float y);
    void End(SDL_FingerID id);
    void Publish();
    bool Visible() const;
    void ButtonCentre(int index, float& x, float& y, float& r) const;

    VirtualPads* pads_ = nullptr;
    int slot_ = -1;
    float width_ = 1, height_ = 1;
    std::map<SDL_FingerID, Finger> fingers_;
    Uint64 last_touch_ = 0;
    bool pad_in_use_ = false;
};

}
