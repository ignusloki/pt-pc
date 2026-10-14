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
    const char* button;  // VirtualPads button name
};

constexpr ButtonDef kButtons[] = {
    {"X", 1.9f, 2.0f, 0.95f, 3, "cross"},     {"O", 3.9f, 1.3f, 0.7f, 3, "circle"},
    {"ZOOM", 1.6f, 4.3f, 0.75f, 3, "r1"},     {"II", 1.2f, 1.0f, 0.55f, 0, "options"},
    {"PC", 2.6f, 1.0f, 0.5f, 0, "share"},  // the port's settings page (Share / View on a controller)
};
constexpr int kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

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
    // a real controller's buttons hide the overlay until the screen is touched again (only use counts: phones list keyboards
    // and remotes as gamepads)
    return pads_ && !pad_in_use_;
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
    static const bool debug = std::getenv("PT_DEBUG_TOUCH") != nullptr;
    if (debug && event.type >= SDL_EVENT_FINGER_DOWN && event.type <= SDL_EVENT_FINGER_CANCELED && event.type != SDL_EVENT_FINGER_MOTION) {
        LogInfo("touch: {} finger {:x} at {:.3f} {:.3f}, {} held before", event.type == SDL_EVENT_FINGER_DOWN ? "down" : event.type == SDL_EVENT_FINGER_UP ? "up" : "cancel",
                static_cast<uint64_t>(event.tfinger.fingerID), event.tfinger.x, event.tfinger.y, fingers_.size());
    }
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);  // points, as ImGui draws
    if (w > 0 && h > 0) {
        width_ = static_cast<float>(w);
        height_ = static_cast<float>(h);
    }
    switch (event.type) {
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (!SDL_IsJoystickVirtual(event.gbutton.which)) pad_in_use_ = true;
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (!SDL_IsJoystickVirtual(event.gaxis.which) && std::abs(static_cast<int>(event.gaxis.value)) > 16000) pad_in_use_ = true;
            break;
        case SDL_EVENT_FINGER_DOWN:
            pad_in_use_ = false;
            Begin(event.tfinger.fingerID, event.tfinger.x * width_, event.tfinger.y * height_);
            break;
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
    if (it->second.role == Role::Look) {
        look_ += glm::vec2(x - it->second.x, y - it->second.y) / height_;
    }
    it->second.x = x;
    it->second.y = y;
    if (it->second.role == Role::Move) Publish();
}

void TouchControls::End(SDL_FingerID id) {
    if (fingers_.erase(id)) Publish();
}

void TouchControls::Publish() {
    if (!pads_) return;
    const float range = 1.2f * height_ / 10.0f;
    float lx = 0, ly = 0;
    bool held[kButtonCount] = {};
    for (const auto& [id, f] : fingers_) {
        if (f.role == Role::Button) {
            held[f.button] = true;
            continue;
        }
        if (f.role != Role::Move) continue;
        float dx = (f.x - f.x0) / range, dy = (f.y - f.y0) / range;
        const float len = std::hypot(dx, dy);
        if (len > 1.0f) {
            dx /= len;
            dy /= len;
        }
        if (f.role == Role::Move) {
            lx = dx;
            ly = dy;
        }
    }
    pads_->SetAxis(slot_, "lx", lx);
    pads_->SetAxis(slot_, "ly", ly);
    pads_->SetAxis(slot_, "rx", 0.0f);
    pads_->SetAxis(slot_, "ry", 0.0f);
    for (int i = 0; i < kButtonCount; ++i) {
        pads_->SetButton(slot_, kButtons[i].button, held[i]);
    }
}

glm::vec2 TouchControls::TakeLook() {
    const glm::vec2 look = look_;
    look_ = glm::vec2(0.0f);
    return look;
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
        if (f.role == Role::Look) {
            look = true;
            list->AddCircle(ImVec2(f.x, f.y), 0.6f * u, IM_COL32(255, 255, 255, 60), 32, 2.0f);
            continue;
        }
        if (f.role != Role::Move) continue;
        move = true;
        const float range = 1.2f * u, dx = f.x - f.x0, dy = f.y - f.y0, len = std::max(range, std::hypot(dx, dy));
        list->AddCircleFilled(ImVec2(f.x0, f.y0), range, fill, 48);
        list->AddCircle(ImVec2(f.x0, f.y0), range, ring, 48, std::max(2.0f, u * 0.04f));
        list->AddCircleFilled(ImVec2(f.x0 + dx * range / len, f.y0 + dy * range / len), 0.45f * u, IM_COL32(200, 200, 200, 110), 32);
    }
    // where the sticks usually sit
    if (!move) list->AddCircle(ImVec2(2.8f * u, height - 2.8f * u), 1.2f * u, IM_COL32(255, 255, 255, 40), 48, 2.0f);
    (void)look;  // the right half has no stick: a drag turns the view
}

}  // namespace pt
