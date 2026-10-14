#include "engine/platform/touch_controls.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "engine/core/log.h"
#include "engine/platform/virtual_pad.h"

namespace pt {
namespace {

// A button: label, centre in units of the screen height / 10 from a corner (0 top left, 1 top right, 2 bottom left,
// 3 bottom right), radius in the same units, and what it presses on the virtual pad.
struct ButtonDef {
    const char* label;
    float cx, cy, r;
    int corner;
    const char* button;  // VirtualPads button name, or nullptr for JACK (both triggers)
};

constexpr ButtonDef kButtons[] = {
    {"X", 1.9f, 2.0f, 0.95f, 3, "cross"},     {"O", 3.9f, 1.3f, 0.7f, 3, "circle"},
    {"ZOOM", 1.6f, 4.3f, 0.75f, 3, "r1"},     {"II", 1.2f, 1.0f, 0.55f, 0, "options"},
    {"JACK", 4.0f, 3.4f, 0.65f, 3, nullptr},
};
constexpr int kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

bool ControllerInUse() {
    // a real controller connected (the virtual pad is one of the gamepads too)
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    bool real = false;
    for (int i = 0; ids && i < count; ++i) {
        if (!SDL_IsJoystickVirtual(ids[i])) real = true;
    }
    SDL_free(ids);
    return real;
}

}  // namespace

bool TouchControls::Wanted() {
    if (const char* env = std::getenv("PT_TOUCH"); env && *env) return *env != '0';
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    return true;
#else
    return false;
#endif
}

void TouchControls::Attach(VirtualPads& pads, int slot) {
    pads_ = &pads;
    slot_ = slot;
    if (!pads.Attach(slot, "ps5")) {
        LogWarn("touch controls: no virtual pad");
        pads_ = nullptr;
        return;
    }
    LogInfo("touch controls: on (virtual pad {})", slot);
}

bool TouchControls::Visible() const {
    // a real controller hides the overlay until the screen is touched again
    return pads_ && (!ControllerInUse() || last_touch_ != 0);
}

void TouchControls::ButtonCentre(int index, float& x, float& y, float& r) const {
    const float u = height_ / 10.0f;
    const ButtonDef& b = kButtons[index];
    x = (b.corner & 1) ? width_ - b.cx * u : b.cx * u;
    y = (b.corner & 2) ? height_ - b.cy * u : b.cy * u;
    r = b.r * u;
}

void TouchControls::ProcessEvent(const SDL_Event& event, SDL_Window* window) {
    if (!pads_) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);  // points, as ImGui draws
    if (w > 0 && h > 0) {
        width_ = static_cast<float>(w);
        height_ = static_cast<float>(h);
    }
    switch (event.type) {
        case SDL_EVENT_FINGER_DOWN: Begin(event.tfinger.fingerID, event.tfinger.x * width_, event.tfinger.y * height_); break;
        case SDL_EVENT_FINGER_MOTION: Move(event.tfinger.fingerID, event.tfinger.x * width_, event.tfinger.y * height_); break;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: End(event.tfinger.fingerID); break;
        default: break;
    }
}

void TouchControls::Begin(SDL_FingerID id, float x, float y) {
    last_touch_ = SDL_GetTicks();
    Finger f;
    f.x0 = f.x = x;
    f.y0 = f.y = y;
    float best = 1e9f;
    for (int i = 0; i < kButtonCount; ++i) {
        float cx, cy, r;
        ButtonCentre(i, cx, cy, r);
        const float d = std::hypot(x - cx, y - cy) / r;
        if (d < 1.25f && d < best) {
            best = d;
            f.role = Role::Button;
            f.button = i;
        }
    }
    if (f.role == Role::None) f.role = x < width_ * 0.45f ? Role::Move : Role::Look;
    fingers_[id] = f;
    Publish();
}

void TouchControls::Move(SDL_FingerID id, float x, float y) {
    auto it = fingers_.find(id);
    if (it == fingers_.end()) return;
    it->second.x = x;
    it->second.y = y;
    if (it->second.role != Role::Button) Publish();
}

void TouchControls::End(SDL_FingerID id) {
    if (fingers_.erase(id)) Publish();
}

void TouchControls::Publish() {
    if (!pads_) return;
    const float range = 1.2f * height_ / 10.0f;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    bool held[kButtonCount] = {};
    for (const auto& [id, f] : fingers_) {
        if (f.role == Role::Button) {
            held[f.button] = true;
            continue;
        }
        float dx = (f.x - f.x0) / range, dy = (f.y - f.y0) / range;
        const float len = std::hypot(dx, dy);
        if (len > 1.0f) {
            dx /= len;
            dy /= len;
        }
        if (f.role == Role::Move) {
            lx = dx;
            ly = dy;
        } else {
            rx = dx;
            ry = dy;
        }
    }
    pads_->SetAxis(slot_, "lx", lx);
    pads_->SetAxis(slot_, "ly", ly);
    pads_->SetAxis(slot_, "rx", rx);
    pads_->SetAxis(slot_, "ry", ry);
    bool jack = false;
    for (int i = 0; i < kButtonCount; ++i) {
        if (kButtons[i].button) {
            pads_->SetButton(slot_, kButtons[i].button, held[i]);
        } else {
            jack = held[i];
        }
    }
    pads_->SetAxis(slot_, "l2", jack ? 1.0f : 0.0f);
    pads_->SetAxis(slot_, "r2", jack ? 1.0f : 0.0f);
}

void TouchControls::Draw(ImDrawList* list, float width, float height) {
    if (!Visible() || !list) return;
    width_ = width;
    height_ = height;
    const float u = height / 10.0f;
    const ImU32 ring = IM_COL32(255, 255, 255, 110), fill = IM_COL32(20, 20, 20, 70), pressed = IM_COL32(170, 150, 120, 120);
    for (int i = 0; i < kButtonCount; ++i) {
        float cx, cy, r;
        ButtonCentre(i, cx, cy, r);
        bool down = false;
        for (const auto& [id, f] : fingers_) down |= f.role == Role::Button && f.button == i;
        list->AddCircleFilled(ImVec2(cx, cy), r, down ? pressed : fill, 40);
        list->AddCircle(ImVec2(cx, cy), r, ring, 40, std::max(2.0f, u * 0.04f));
        const char* label = kButtons[i].label;
        const float size = r * (std::strlen(label) > 2 ? 0.45f : 0.7f);
        const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        list->AddText(ImGui::GetFont(), size, ImVec2(cx - text.x / 2, cy - text.y / 2), IM_COL32(255, 255, 255, 200), label);
    }
    bool move = false, look = false;
    for (const auto& [id, f] : fingers_) {
        if (f.role != Role::Move && f.role != Role::Look) continue;
        (f.role == Role::Move ? move : look) = true;
        const float range = 1.2f * u, dx = f.x - f.x0, dy = f.y - f.y0, len = std::max(range, std::hypot(dx, dy));
        list->AddCircleFilled(ImVec2(f.x0, f.y0), range, fill, 48);
        list->AddCircle(ImVec2(f.x0, f.y0), range, ring, 48, std::max(2.0f, u * 0.04f));
        list->AddCircleFilled(ImVec2(f.x0 + dx * range / len, f.y0 + dy * range / len), 0.45f * u, IM_COL32(200, 200, 200, 110), 32);
    }
    // where the sticks usually sit
    if (!move) list->AddCircle(ImVec2(2.8f * u, height - 2.8f * u), 1.2f * u, IM_COL32(255, 255, 255, 40), 48, 2.0f);
    if (!look) list->AddCircle(ImVec2(width * 0.62f, height - 2.8f * u), 1.2f * u, IM_COL32(255, 255, 255, 25), 48, 2.0f);
}

}  // namespace pt
