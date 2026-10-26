#pragma once

// On-screen touch controls for phones and tablets (docs/mobile.md): a virtual DualSense the game reads like any controller.
// Left half: a floating stick that walks; right half: drags that turn the view (as a mouse does); buttons for Cross, Circle, R1 (zoom),
// Options and PC (the port's settings, Share); the word at the end is heard through the phone's microphone. Hidden once a real
// controller is used; a touch brings them back. PT_TOUCH=1 turns them on elsewhere (the mouse acts as a finger), PT_TOUCH=0 off.

#include <SDL3/SDL.h>

#include <glm/vec2.hpp>

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
    // the look drags since the last call, in screen heights (x right, y down)
    glm::vec2 TakeLook();
    void Reset();
    bool TakeColumn();
    void SetMenuOpen(bool open);

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
    SDL_Rect safe_{0, 0, 1, 1};
    bool menu_open_ = false;
    bool column_ = false;
    std::map<SDL_FingerID, Finger> fingers_;
    glm::vec2 look_{0.0f};
    Uint64 last_touch_ = 0;
    bool pad_in_use_ = false;
};

}
