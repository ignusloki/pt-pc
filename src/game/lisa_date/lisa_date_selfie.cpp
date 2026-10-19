#include "game/lisa_date/lisa_date.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iterator>

#include "engine/anim/skeleton.h"
#include "engine/core/log.h"
#include "engine/render/texture_manager.h"
#include "engine/ui/text_layout.h"
#include "game/game.h"
#include "game/lisa_date/lisa_date_audio.h"
#include "game/player_animation.h"
#include "game/ui/ui_assets.h"

namespace pt::game {
namespace {

constexpr float kPolaroidSize = 0.8f * UiCanvas::kHeight;
constexpr float kPolaroidPixels = 1254.0f;
const glm::vec2 kWindowMiddle(0.5f * UiCanvas::kWidth, 0.38f * UiCanvas::kHeight);
const glm::vec2 kPolaroidMiddle = kWindowMiddle - glm::vec2(3.5f, -41.0f) / kPolaroidPixels * kPolaroidSize;
const glm::vec2 kWindowLo = kPolaroidMiddle + glm::vec2(240.0f - 627.0f, 218.0f - 627.0f) / kPolaroidPixels * kPolaroidSize;
const glm::vec2 kWindowHi = kPolaroidMiddle + glm::vec2(1021.0f - 627.0f, 954.0f - 627.0f) / kPolaroidPixels * kPolaroidSize;
constexpr float kDevelop = 2.6f;
constexpr float kSaveWait = 1.5f;
const glm::vec4 kInk(0.30f, 0.16f, 0.22f, 1.0f);
const glm::vec4 kRose(0.93f, 0.45f, 0.62f, 1.0f);
const glm::vec4 kLine(0.96f, 0.6f, 0.74f, 1.0f);

float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Level(const float (&table)[5], float level) {
    const float x = std::clamp(level, -2.0f, 2.0f) + 2.0f;
    const int i = std::min(3, static_cast<int>(std::floor(x)));
    return table[i] + (table[i + 1] - table[i]) * (x - static_cast<float>(i));
}

glm::quat Arc(const glm::vec3& u, const glm::vec3& v) {
    const float c = glm::dot(u, v);
    if (c < -0.99999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), u);
        if (glm::length(axis) < 1.0e-4f) axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), u);
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(u, v);
    return glm::normalize(glm::quat(1.0f + c, axis.x, axis.y, axis.z));
}

struct Framing {
    float azimuth = 24.0f;
    float elevation = 14.0f;
    float distance = 0.50f;
    float fov = 74.0f;
    glm::vec3 offset{0.22f, 0.0f, -0.08f};
    float lean = 10.0f;
    float bend = 11.0f;
};

const Framing& SelfieFraming() {
    static const Framing f = [] {
        Framing out;
        if (const char* v = std::getenv("PT_LISA_SELFIE")) {
            std::sscanf(v, "%f,%f,%f,%f,%f,%f,%f,%f,%f", &out.azimuth, &out.elevation, &out.distance, &out.fov, &out.offset.x, &out.offset.y,
                        &out.offset.z, &out.lean, &out.bend);
            LogInfo("lisa date: selfie framing from PT_LISA_SELFIE ({})", v);
        }
        return out;
    }();
    return f;
}

struct FaceMove {
    const char* bone;
    glm::vec3 smile;
    glm::vec3 frown;
    glm::vec3 grin;
};
const FaceMove kHisFace[] = {
    {"SKL_409_LPML", {0.0045f, 0.0060f, -0.0040f}, {-0.0010f, -0.0055f, 0.0f}, {0.0015f, 0.0010f, -0.0015f}},
    {"SKL_410_LPMR", {-0.0045f, 0.0060f, -0.0040f}, {0.0010f, -0.0055f, 0.0f}, {-0.0015f, 0.0010f, -0.0015f}},
    {"SKL_411_LPNL", {0.0020f, 0.0030f, 0.0f}, {0.0f, -0.0010f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_413_LPNR", {-0.0020f, 0.0030f, 0.0f}, {0.0f, -0.0010f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_412_LPCL", {0.0030f, 0.0040f, -0.0020f}, {0.0f, -0.0040f, 0.0f}, {0.0f, -0.0040f, 0.0f}},
    {"SKL_414_LPCR", {-0.0030f, 0.0040f, -0.0020f}, {0.0f, -0.0040f, 0.0f}, {0.0f, -0.0040f, 0.0f}},
    {"SKL_405_LPUL", {0.0f, 0.0015f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0020f, 0.0005f}},
    {"SKL_406_LPUR", {0.0f, 0.0015f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0020f, 0.0005f}},
    {"SKL_415_LPUI", {0.0f, 0.0015f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0020f, 0.0005f}},
    {"SKL_407_LPLL", {0.0f, 0.0f, 0.0f}, {0.0f, 0.0020f, 0.0020f}, {0.0f, -0.0080f, 0.0f}},
    {"SKL_408_LPLR", {0.0f, 0.0f, 0.0f}, {0.0f, 0.0020f, 0.0020f}, {0.0f, -0.0080f, 0.0f}},
    {"SKL_416_LPLI", {0.0f, 0.0f, 0.0f}, {0.0f, 0.0020f, 0.0020f}, {0.0f, -0.0080f, 0.0f}},
    {"SKL_404_JAWS", {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, -0.0090f, -0.0030f}},
    {"SKL_421_CHKL", {0.0f, 0.0040f, 0.0010f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_422_CHKR", {0.0f, 0.0040f, 0.0010f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_436_MCKL", {0.0f, 0.0030f, 0.0010f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_437_MCKR", {0.0f, 0.0030f, 0.0010f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_434_EYLL", {0.0f, 0.0010f, 0.0f}, {0.0f, 0.0008f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_435_EYLR", {0.0f, 0.0010f, 0.0f}, {0.0f, 0.0008f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_432_EIUL", {0.0f, 0.0f, 0.0f}, {0.0f, -0.0020f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_433_EIUR", {0.0f, 0.0f, 0.0f}, {0.0f, -0.0020f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_426_EBOL", {0.0f, 0.0020f, 0.0f}, {-0.0030f, -0.0060f, 0.0010f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_427_EBOR", {0.0f, 0.0020f, 0.0f}, {0.0030f, -0.0060f, 0.0010f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_428_EBEL", {0.0f, 0.0010f, 0.0f}, {0.0f, -0.0020f, 0.0f}, {0.0f, 0.0f, 0.0f}},
    {"SKL_429_EBER", {0.0f, 0.0010f, 0.0f}, {0.0f, -0.0020f, 0.0f}, {0.0f, 0.0f, 0.0f}},
};

const char* kFaceKeys[5] = {"ui_face_grumpy", "ui_face_frown", "ui_face_neutral", "ui_face_smile", "ui_face_grin"};
const char* kFaceLog[5] = {"grumpy", "frown", "neutral", "smile", "big grin"};

}

void LisaDate::StartSelfie() {
    selfie_ = Selfie::Panel;
    wait_ = Wait::Selfie;
    selfie_row_ = 0;
    selfie_time_ = 0.0f;
    skip_ = false;
    auto_ = false;
    for (int i = 0; i < 4; ++i) selfie_shown_[i] = static_cast<float>(selfie_values_[i]);
    if (shutter_wav_.empty()) shutter_wav_ = LisaDateWav(LisaDateShutter(0.8f));
    LogInfo("lisa date: selfie panel up ({})", player_mesh_ ? "with the player's model" : "no player model, her alone");
}

void LisaDate::SetSelfie(int lisa_face, int lisa_tilt, int you_face, int you_tilt, bool take) {
    if (selfie_ != Selfie::Panel) return;
    selfie_values_[0] = std::clamp(lisa_face, -2, 2);
    selfie_values_[1] = std::clamp(lisa_tilt, -2, 2);
    selfie_values_[2] = std::clamp(you_face, -2, 2);
    selfie_values_[3] = std::clamp(you_tilt, -2, 2);
    for (int i = 0; i < 4; ++i) selfie_shown_[i] = static_cast<float>(selfie_values_[i]);
    if (take) TakeSelfie();
}

void LisaDate::TakeSelfie() {
    if (selfie_ != Selfie::Panel) return;
    for (int i = 0; i < 4; ++i) selfie_shown_[i] = static_cast<float>(selfie_values_[i]);
    selfie_ = Selfie::Developing;
    selfie_time_ = 0.0f;
    frozen_ = true;
    frozen_time_ = time_;
    photo_look_ = SelfieLook();
    look_ = look_target_ = photo_look_;
    blink_time_ = 0.0f;
    twitch_time_ = 0.0f;
    photo_ = true;
    flash_time_ = 0.5f;
    flash_color_ = glm::vec3(1.0f);
    if (audio_ && !shutter_wav_.empty()) audio_->PlayStream(shutter_wav_, nullptr);
    if (run_) {
        run_->SetVar("selfie:lisa", selfie_values_[0]);
        run_->SetVar("selfie:you", selfie_values_[2]);
    }
    LogInfo("lisa date: photo (selfie: Lisa {} tilt {:+d}, you {} tilt {:+d})", kFaceLog[selfie_values_[0] + 2], selfie_values_[1],
            kFaceLog[selfie_values_[2] + 2], selfie_values_[3]);
}

void LisaDate::PhotoSaved(const std::string& file, bool ok) {
    if (selfie_ != Selfie::Saving) return;
    if (ok) {
        std::string note = text_.Get("ui_photo_saved");
        if (const size_t at = note.find("{file}"); at != std::string::npos) note.replace(at, 6, file);
        photo_note_ = note;
        LogInfo("lisa date: polaroid saved as {}", file);
    } else {
        photo_note_ = text_.Get("ui_photo_failed");
        LogWarn("lisa date: the polaroid could not be saved ({})", file);
    }
    photo_note_time_ = 6.0f;
    selfie_ = Selfie::Shown;
}

void LisaDate::UpdateSelfie(float dt, const InputState& input, bool up, bool down, bool left, bool right, bool accept, bool clicked, bool pointer_moved) {
    for (int i = 0; i < 4; ++i) {
        const float target = static_cast<float>(selfie_values_[i]);
        selfie_shown_[i] = selfie_ == Selfie::Panel ? target + (selfie_shown_[i] - target) * std::exp(-12.0f * dt) : target;
    }
    if (selfie_ == Selfie::Panel) {
        if (!frozen_) player_idle_ += dt * 59.94f;
        int hovered = -1;
        if (pointer_valid_ && selfie_buttons_ready_) {
            for (int r = 0; r < 5; ++r) {
                if (selfie_buttons_.rows[r].Contains(pointer_)) hovered = r;
            }
        }
        if (hovered >= 0 && pointer_moved) selfie_row_ = hovered;
        if (up) selfie_row_ = std::max(0, selfie_row_ - 1);
        if (down) selfie_row_ = std::min(4, selfie_row_ + 1);
        auto change = [&](int row, int by) {
            if (row < 0 || row > 3) return;
            const int before = selfie_values_[row];
            selfie_values_[row] = std::clamp(before + by, -2, 2);
            if (selfie_values_[row] != before) {
                LogInfo("lisa date: selfie {} {} {:+d}", row < 2 ? "Lisa" : "you", row % 2 == 0 ? "expression" : "tilt", selfie_values_[row]);
            }
        };
        if (left) change(selfie_row_, -1);
        if (right) change(selfie_row_, 1);
        bool take = (input.raw_pressed & kRawSquare) != 0 || input.date_skip_pressed;
        if (clicked && pointer_valid_ && selfie_buttons_ready_) {
            for (int r = 0; r < 4; ++r) {
                if (selfie_buttons_.minus[r].Contains(pointer_)) change(r, -1), selfie_row_ = r;
                if (selfie_buttons_.plus[r].Contains(pointer_)) change(r, 1), selfie_row_ = r;
            }
            if (selfie_buttons_.rows[4].Contains(pointer_)) take = true;
        }
        if (accept) {
            if (selfie_row_ == 4) take = true;
            else ++selfie_row_;
        }
        look_target_ = SelfieLook();
        if (take) TakeSelfie();
        return;
    }
    selfie_time_ += dt;
    if (selfie_ == Selfie::Developing && selfie_time_ >= kDevelop) {
        selfie_ = Selfie::Saving;
        saving_time_ = 0.0f;
        if (canvas_ready_) {
            const glm::vec2 full = full_;
            const glm::vec2 lo = canvas_.ToTarget(kPolaroidMiddle - glm::vec2(0.5f * kPolaroidSize));
            const glm::vec2 hi = canvas_.ToTarget(kPolaroidMiddle + glm::vec2(0.5f * kPolaroidSize));
            const glm::vec2 a = glm::clamp(lo / full, glm::vec2(0.0f), glm::vec2(1.0f));
            const glm::vec2 b = glm::clamp(hi / full, glm::vec2(0.0f), glm::vec2(1.0f));
            photo_request_ = glm::vec4(a, b - a);
        }
        LogInfo("lisa date: selfie developed, saving the polaroid");
    } else if (selfie_ == Selfie::Saving) {
        saving_time_ += dt;
        if (saving_time_ >= kSaveWait) {
            LogWarn("lisa date: the polaroid was not saved (no frame took it)");
            selfie_ = Selfie::Shown;
            photo_request_.reset();
        }
    }
    if (selfie_ == Selfie::Shown && wait_ == Wait::Selfie) {
        wait_ = Wait::None;
        AdvanceScript();
    }
}

LisaDate::Look LisaDate::SelfieLook() const {
    static constexpr float kSmile[5] = {-1.1f, -0.6f, 0.1f, 0.8f, 1.2f};
    static constexpr float kJaw[5] = {0.05f, 0.0f, 0.0f, 0.12f, 0.5f};
    static constexpr float kLids[5] = {0.45f, 0.25f, 0.1f, 0.25f, 0.35f};
    static constexpr float kPitch[5] = {4.0f, 2.0f, 0.0f, 0.0f, -2.0f};
    const Framing& f = SelfieFraming();
    const float face = selfie_shown_[0];
    const float tilt = selfie_shown_[1];
    Look l;
    l.lean = 4.0f;
    l.pitch = Level(kPitch, face) - 0.5f * f.elevation;
    l.roll = -9.0f * tilt;
    l.yaw = 0.7f * f.azimuth;
    l.smile = Level(kSmile, face);
    l.jaw = Level(kJaw, face);
    l.lids = Level(kLids, face);
    l.zoom = 1.0f;
    l.tint = glm::vec3(1.05f, 0.98f, 0.97f);
    return l;
}

void LisaDate::PosePlayer() {
    selfie_camera_ = false;
    if (selfie_ == Selfie::Off || skin_.empty() || bind_.empty()) return;
    const Framing& f = SelfieFraming();
    int her_head = -1;
    for (size_t i = 0; i < bone_names_.size(); ++i) {
        if (bone_names_[i] == "SKL_004_HEAD") her_head = static_cast<int>(i);
    }
    if (her_head < 0) return;
    const glm::vec3 head_l = glm::vec3(skin_[static_cast<size_t>(her_head)] * glm::vec4(bind_[static_cast<size_t>(her_head)], 1.0f));
    const glm::vec3 face_l = head_l + glm::vec3(0.0f, 0.085f, 0.10f);
    const glm::vec3 head_n = head_l + f.offset;
    const glm::vec3 face_n = head_n + glm::vec3(0.0f, 0.095f, 0.095f);
    const glm::vec3 mid = 0.5f * (face_l + face_n);
    const float az = glm::radians(f.azimuth), el = glm::radians(f.elevation);
    const glm::vec3 eye = mid + f.distance * glm::vec3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
    const glm::vec3 to_mid = glm::normalize(mid - eye);
    const glm::vec3 cam_right = glm::normalize(glm::cross(to_mid, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 cam_up = glm::cross(cam_right, to_mid);
    const float window_y = 1.0f - 2.0f * kWindowMiddle.y / UiCanvas::kHeight;
    const float down = std::atan(window_y * std::tan(glm::radians(f.fov) * 0.5f));
    const glm::vec3 forward = glm::normalize(std::cos(down) * to_mid - std::sin(down) * cam_up);
    selfie_eye_ = eye;
    selfie_target_ = eye + forward;
    selfie_fov_ = f.fov;
    selfie_camera_ = true;
    if (!player_mesh_) return;

    const float yaw = 0.8f * std::atan2(eye.x - head_n.x, eye.z - head_n.z);
    const float tilt = selfie_shown_[3];
    const float face = selfie_shown_[2];
    constexpr float kScale = 0.9f;
    glm::mat4 placed(1.0f);
    glm::mat3 head_rotation(1.0f);
    std::vector<int> face_bones;
    const PlayerPoseEdit edit = [&](const anim::Skeleton& skeleton, std::vector<glm::mat4>& world) {
        auto bone = [&](std::string_view name) { return skeleton.FindName(name); };
        auto under = [&](size_t b, int root) {
            for (int p = static_cast<int>(b); p >= 0; p = skeleton.parents[static_cast<size_t>(p)]) {
                if (p == root) return true;
            }
            return false;
        };
        auto turn = [&](int root, const glm::quat& q, const glm::vec3& pivot) {
            if (root < 0) return;
            const glm::mat4 m = glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(q) * glm::translate(glm::mat4(1.0f), -pivot);
            for (size_t i = 0; i < world.size(); ++i) {
                if (under(i, root)) world[i] = m * world[i];
            }
        };
        auto at = [&](int b) { return glm::vec3(world[static_cast<size_t>(b)][3]); };
        const int spine = bone("SKL_001_SPINE"), chest = bone("SKL_002_CHEST"), neck = bone("SKL_003_NECK"), head = bone("SKL_004_HEAD");
        const int upper = bone("SKL_011_LUARM"), lower = bone("SKL_012_LFARM"), hand = bone("SKL_013_LHAND");
        if (spine < 0 || chest < 0 || neck < 0 || head < 0) return;
        const glm::quat lean = glm::angleAxis(glm::radians(0.6f * f.lean), glm::vec3(1.0f, 0.0f, 0.0f)) *
                               glm::angleAxis(glm::radians(0.6f * f.bend), glm::vec3(0.0f, 0.0f, 1.0f));
        turn(spine, lean, at(spine));
        const glm::quat lean_chest = glm::angleAxis(glm::radians(0.4f * f.lean), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                     glm::angleAxis(glm::radians(0.4f * f.bend), glm::vec3(0.0f, 0.0f, 1.0f));
        turn(chest, lean_chest, at(chest));
        const float roll = glm::radians(8.0f * tilt);
        const float pitch = glm::radians(-(f.lean + 0.8f * f.elevation) + (face < 0.0f ? -2.5f * face : 0.0f));
        turn(neck, glm::angleAxis(0.4f * roll, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(0.4f * pitch, glm::vec3(1.0f, 0.0f, 0.0f)), at(neck));
        turn(head, glm::angleAxis(0.6f * roll, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(0.6f * pitch, glm::vec3(1.0f, 0.0f, 0.0f)), at(head));
        const glm::mat4 turn_scale = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(kScale));
        const glm::vec3 head_model = at(head);
        placed = glm::translate(glm::mat4(1.0f), head_n - glm::vec3(turn_scale * glm::vec4(head_model, 1.0f))) * turn_scale;
        if (upper >= 0 && lower >= 0 && hand >= 0) {
            const glm::vec3 wrist_scene = eye + cam_right * 0.05f - cam_up * 0.09f - to_mid * 0.06f;
            const glm::vec3 target = glm::vec3(glm::inverse(placed) * glm::vec4(wrist_scene, 1.0f));
            const glm::vec3 p0 = at(upper), p1 = at(lower), p2 = at(hand);
            const float a = glm::length(p1 - p0), b = glm::length(p2 - p1);
            const glm::vec3 reach = target - p0;
            const float d = std::clamp(glm::length(reach), std::abs(a - b) + 1.0e-3f, a + b - 1.0e-3f);
            const glm::vec3 dir = glm::normalize(reach);
            glm::vec3 pole = glm::vec3(0.4f, -1.0f, 0.0f);
            pole = glm::normalize(pole - glm::dot(pole, dir) * dir);
            const float cos_a = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
            const float sin_a = std::sqrt(std::max(0.0f, 1.0f - cos_a * cos_a));
            const glm::vec3 elbow = p0 + a * (cos_a * dir + sin_a * pole);
            turn(upper, Arc(glm::normalize(p1 - p0), glm::normalize(elbow - p0)), p0);
            const glm::vec3 e = at(lower), w = at(hand);
            turn(lower, Arc(glm::normalize(w - e), glm::normalize(p0 + dir * d - e)), e);
        }
        head_rotation = glm::mat3(world[static_cast<size_t>(head)]);
        const glm::vec3 lens = glm::vec3(glm::inverse(placed) * glm::vec4(eye, 1.0f));
        const glm::vec3 looks = glm::normalize(glm::vec3(world[static_cast<size_t>(head)] * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
        for (const char* name : {"SKL_430_EYEL", "SKL_431_EYER"}) {
            const int e = bone(name);
            if (e < 0) continue;
            const glm::vec3 to = glm::normalize(lens - at(e));
            const float angle = std::acos(std::clamp(glm::dot(looks, to), -1.0f, 1.0f));
            const float limit = glm::radians(25.0f);
            const glm::vec3 aim = angle > limit ? glm::normalize(glm::mix(looks, to, limit / angle)) : to;
            turn(e, Arc(looks, aim), at(e));
        }
        for (const FaceMove& m : kHisFace) face_bones.push_back(skeleton.FindName(m.bone));
    };
    if (!PlayerPoseSkin(player_help_.get(), player_idle_, edit, player_skin_)) {
        player_skin_.clear();
        return;
    }
    player_transform_ = placed;
    const float smile = std::clamp(face, 0.0f, 2.0f) * 0.5f;
    const float frown = std::clamp(-face, 0.0f, 2.0f) * 0.5f;
    const float grin = std::clamp(face - 1.0f, 0.0f, 1.0f);
    for (size_t i = 0; i < face_bones.size() && i < std::size(kHisFace); ++i) {
        const int b = face_bones[i];
        if (b < 0 || static_cast<size_t>(b) >= player_skin_.size()) continue;
        const FaceMove& m = kHisFace[i];
        const glm::vec3 offset = head_rotation * (1.8f * (smile * m.smile + frown * m.frown) + 1.3f * grin * m.grin);
        player_skin_[static_cast<size_t>(b)] = glm::translate(glm::mat4(1.0f), offset) * player_skin_[static_cast<size_t>(b)];
    }
}

float LisaDate::DrawPrompt(ui::UiBatch& batch, const UiCanvas& canvas, PromptButton button, KeyAction key, glm::vec2 at, float height, float alpha,
                          bool from_right, KeyAction key2) {
    const PromptGlyph glyph = assets_.PromptPicture({button, key, key2}, prompts_);
    if (glyph.icon.empty()) return 0.0f;
    const float cell = height / std::max(0.05f, glyph.body_height);
    const float body = cell * glyph.body_width;
    const glm::vec2 centre(from_right ? at.x - 0.5f * body : at.x + 0.5f * body, at.y);
    const glm::vec2 half(0.5f * cell * glyph.width, 0.5f * cell);
    auto texture = [&](const std::string& name) {
        const uint32_t t = assets_.Texture(name);
        return t | assets_.TextureAddressBits(t);
    };
    const glm::vec2 a = canvas.ToTarget(centre - half), b = canvas.ToTarget(centre + half);
    if (!glyph.glow.empty()) {
        batch.Quad(a, b, glyph.uv0, glyph.uv1, glm::vec4(1.0f, 1.0f, 1.0f, 0.4f * alpha), ui::UiDrawParams::Plain(texture(glyph.glow)), ui::UiShade::Material,
                   ui::UiBlend::Additive);
    }
    batch.Quad(a, b, glyph.uv0, glyph.uv1, glm::vec4(1.0f, 1.0f, 1.0f, alpha), ui::UiDrawParams::Plain(texture(glyph.icon)), ui::UiShade::Material,
               ui::UiBlend::Alpha);
    return body;
}

float LisaDate::DrawHint(ui::UiBatch& batch, const UiCanvas& canvas, PromptButton button, KeyAction key, const std::string& words, glm::vec2 at, float alpha,
                        KeyAction key2) {
    const float size = 16.0f;
    const float glyph = DrawPrompt(batch, canvas, button, key, at, 21.0f, alpha, false, key2);
    float width = 8.0f * static_cast<float>(words.size());
    if (UiFont* font = assets_.Font(UiFontType::PcSystem, std::clamp(settings_.language, 0, UiAssets::kLanguageCount - 1)); font_ || font) {
        ui::TextStyle style;
        style.font = &(font_ ? font_ : font)->font;
        style.font_width = style.font_height = size;
        width = ui::LayoutText(words, style, 2000.0f).width;
    }
    const glm::vec2 lo(at.x + glyph + 6.0f, at.y - 0.6f * size), hi(lo.x + width + 4.0f, at.y + 0.6f * size);
    DrawLabel(batch, canvas, words, lo, hi, size, glm::vec4(glm::vec3(0.36f, 0.26f, 0.42f), alpha), ui::TextAlign::Start);
    return hi.x;
}

void LisaDate::DrawArrow(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 center, float size, bool right, glm::vec4 color) {
    const float s = right ? 1.0f : -1.0f;
    const ui::UiVertex v[] = {
        {canvas.ToTarget(center + glm::vec2(s * 0.5f * size, 0.0f)), {0, 0}, color},
        {canvas.ToTarget(center + glm::vec2(-s * 0.4f * size, -0.55f * size)), {0, 0}, color},
        {canvas.ToTarget(center + glm::vec2(-s * 0.4f * size, 0.55f * size)), {0, 0}, color},
    };
    batch.Draw(v, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
}

void LisaDate::DrawSelfiePanel(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full) {
    const auto plain = ui::UiDrawParams::Plain(TextureManager::kWhite);
    const glm::vec2 wlo = canvas.ToTarget(kWindowLo), whi = canvas.ToTarget(kWindowHi);
    const glm::vec4 dim(0.08f, 0.04f, 0.08f, 0.42f);
    batch.Quad({0.0f, 0.0f}, {full.x, wlo.y}, {0, 0}, {1, 1}, dim, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    batch.Quad({0.0f, whi.y}, full, {0, 0}, {1, 1}, dim, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    batch.Quad({0.0f, wlo.y}, {wlo.x, whi.y}, {0, 0}, {1, 1}, dim, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    batch.Quad({whi.x, wlo.y}, {full.x, whi.y}, {0, 0}, {1, 1}, dim, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    const float mark = 34.0f, thick = 4.0f;
    const glm::vec4 white(1.0f, 1.0f, 1.0f, 0.95f);
    for (int corner = 0; corner < 4; ++corner) {
        const glm::vec2 c(corner % 2 ? kWindowHi.x : kWindowLo.x, corner / 2 ? kWindowHi.y : kWindowLo.y);
        const glm::vec2 inward(corner % 2 ? -1.0f : 1.0f, corner / 2 ? -1.0f : 1.0f);
        DrawBox(batch, canvas, glm::min(c, c + inward * glm::vec2(mark, thick)), glm::max(c, c + inward * glm::vec2(mark, thick)), white, glm::vec4(0.0f), 0.0f);
        DrawBox(batch, canvas, glm::min(c, c + inward * glm::vec2(thick, mark)), glm::max(c, c + inward * glm::vec2(thick, mark)), white, glm::vec4(0.0f), 0.0f);
    }

    const glm::vec2 lo(36.0f, 92.0f), hi(428.0f, 594.0f);
    DrawBox(batch, canvas, lo, hi, glm::vec4(1.0f, 0.97f, 0.98f, 0.94f), kLine, 4.0f);
    DrawLabel(batch, canvas, text_.Get("ui_selfie"), {lo.x + 16.0f, lo.y + 10.0f}, {hi.x - 16.0f, lo.y + 50.0f}, 30.0f, kRose, ui::TextAlign::Center);
    const char* sections[2] = {"name_lisa", "name_you"};
    for (int row = 0; row < 5; ++row) {
        const float top = row < 2 ? 196.0f + 54.0f * static_cast<float>(row) : row < 4 ? 340.0f + 54.0f * static_cast<float>(row - 2) : 474.0f;
        if (row == 0 || row == 2) {
            const float plate = top - 40.0f;
            DrawBox(batch, canvas, {lo.x + 22.0f, plate}, {lo.x + 172.0f, plate + 32.0f}, glm::vec4(1.0f, 0.7f, 0.82f, 1.0f), glm::vec4(1.0f), 2.0f);
            DrawLabel(batch, canvas, text_.Get(sections[row / 2]), {lo.x + 26.0f, plate + 2.0f}, {lo.x + 168.0f, plate + 30.0f}, 21.0f, glm::vec4(1.0f),
                      ui::TextAlign::Center);
        }
        const bool on = row == selfie_row_;
        if (row == 4) {
            Button& b = selfie_buttons_.rows[4];
            b = {{lo.x + 30.0f, top}, {hi.x - 30.0f, top + 60.0f}};
            DrawBox(batch, canvas, b.lo, b.hi, on ? glm::vec4(0.96f, 0.5f, 0.7f, 0.98f) : glm::vec4(1.0f, 0.86f, 0.92f, 0.95f), kLine, on ? 4.0f : 2.0f);
            const glm::vec2 icon(b.lo.x + 40.0f, 0.5f * (b.lo.y + b.hi.y));
            const glm::vec4 ink = on ? glm::vec4(1.0f) : kInk;
            DrawBox(batch, canvas, icon + glm::vec2(-17.0f, -10.0f), icon + glm::vec2(17.0f, 13.0f), ink, glm::vec4(0.0f), 0.0f);
            DrawBox(batch, canvas, icon + glm::vec2(-7.0f, -15.0f), icon + glm::vec2(7.0f, -9.0f), ink, glm::vec4(0.0f), 0.0f);
            DrawHeart(batch, canvas.ToTarget(icon + glm::vec2(0.0f, 1.5f)), 15.0f * canvas.scale, on ? glm::vec4(0.96f, 0.5f, 0.7f, 1.0f) : glm::vec4(1.0f, 0.86f, 0.92f, 1.0f));
            float right = b.hi.x - 10.0f;
            right -= DrawPrompt(batch, canvas, PromptButton::Square, KeyAction::DateSkip, {right, 0.5f * (b.lo.y + b.hi.y)}, 20.0f, 1.0f, true) + 6.0f;
            if (on) right -= DrawPrompt(batch, canvas, PromptButton::Cross, KeyAction::Confirm, {right, 0.5f * (b.lo.y + b.hi.y)}, 20.0f, 1.0f, true) + 6.0f;
            DrawLabel(batch, canvas, text_.Get("ui_selfie_take"), {b.lo.x + 66.0f, b.lo.y + 8.0f}, {right - 4.0f, b.hi.y - 8.0f}, 24.0f, ink, ui::TextAlign::Center);
            const float y = b.hi.y + 28.0f;
            float x = DrawPrompt(batch, canvas, PromptButton::DpadUpDown, KeyAction::PadUp, {lo.x + 30.0f, y}, 34.0f, 1.0f, false, KeyAction::PadDown);
            x += lo.x + 42.0f;
            DrawPrompt(batch, canvas, PromptButton::DpadLeftRight, KeyAction::PadLeft, {x, y}, 34.0f, 1.0f, false, KeyAction::PadRight);
            continue;
        }
        Button& r = selfie_buttons_.rows[row];
        r = {{lo.x + 14.0f, top}, {hi.x - 14.0f, top + 46.0f}};
        if (on) DrawBox(batch, canvas, r.lo, r.hi, glm::vec4(1.0f, 0.78f, 0.88f, 0.9f), kLine, 2.0f);
        DrawLabel(batch, canvas, text_.Get(row % 2 == 0 ? "ui_selfie_face" : "ui_selfie_tilt"), {r.lo.x + 10.0f, r.lo.y + 4.0f},
                  {r.lo.x + 160.0f, r.hi.y - 4.0f}, 19.0f, kInk, ui::TextAlign::Start);
        const float vx0 = r.lo.x + 168.0f, vx1 = r.hi.x - 6.0f, cy = 0.5f * (r.lo.y + r.hi.y);
        selfie_buttons_.minus[row] = {{vx0, r.lo.y}, {vx0 + 34.0f, r.hi.y}};
        selfie_buttons_.plus[row] = {{vx1 - 34.0f, r.lo.y}, {vx1, r.hi.y}};
        const int value = selfie_values_[row];
        DrawArrow(batch, canvas, {vx0 + 17.0f, cy}, 18.0f, false, value > -2 ? kRose : glm::vec4(0.8f, 0.7f, 0.75f, 0.6f));
        DrawArrow(batch, canvas, {vx1 - 17.0f, cy}, 18.0f, true, value < 2 ? kRose : glm::vec4(0.8f, 0.7f, 0.75f, 0.6f));
        if (row % 2 == 0) {
            DrawLabel(batch, canvas, text_.Get(kFaceKeys[value + 2]), {vx0 + 36.0f, r.lo.y + 4.0f}, {vx1 - 36.0f, r.hi.y - 4.0f}, 19.0f, kInk,
                      ui::TextAlign::Center);
        } else {
            const float x0 = vx0 + 50.0f, x1 = vx1 - 50.0f;
            for (int i = 0; i < 5; ++i) {
                const float x = x0 + (x1 - x0) * static_cast<float>(i) / 4.0f;
                const bool here = i - 2 == value;
                DrawHeart(batch, canvas.ToTarget({x, cy}), (here ? 22.0f : 12.0f) * canvas.scale,
                          here ? glm::vec4(1.0f, 0.38f, 0.6f, 1.0f) : glm::vec4(0.75f, 0.6f, 0.68f, 0.7f));
            }
        }
    }
    selfie_buttons_ready_ = true;
}

void LisaDate::DrawSelfieOutside(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full) {
    const auto plain = ui::UiDrawParams::Plain(TextureManager::kWhite);
    const glm::vec2 wlo = canvas.ToTarget(kWindowLo), whi = canvas.ToTarget(kWindowHi);
    const glm::vec2 parts[4][2] = {{{0.0f, 0.0f}, {full.x, wlo.y}}, {{0.0f, whi.y}, full}, {{0.0f, wlo.y}, {wlo.x, whi.y}}, {{whi.x, wlo.y}, {full.x, whi.y}}};
    const glm::vec4 tint(0.72f, 0.62f, 0.66f, 1.0f);
    const glm::vec4 veil(1.0f, 0.82f, 0.9f, 0.18f);
    const float band = std::min(1.0f, backdrop_.aspect / (16.0f / 9.0f));
    const glm::vec2 clo = canvas.origin, chi = canvas.origin + glm::vec2(UiCanvas::kWidth, UiCanvas::kHeight) * canvas.scale;
    auto uv = [&](glm::vec2 p) { return glm::vec2((p.x - clo.x) / (chi.x - clo.x), band * (p.y - clo.y) / (chi.y - clo.y)); };
    for (const auto& part : parts) {
        if (part[1].x - part[0].x < 0.5f || part[1].y - part[0].y < 0.5f) continue;
        batch.Quad(part[0], part[1], {0, 0}, {1, 1}, glm::vec4(0.42f, 0.30f, 0.36f, 1.0f), plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
        if (backdrop_.texture) {
            const glm::vec2 a = glm::clamp(part[0], clo, chi), b = glm::clamp(part[1], clo, chi);
            if (b.x - a.x >= 0.5f && b.y - a.y >= 0.5f) {
                batch.Quad(a, b, uv(a), uv(b), tint, ui::UiDrawParams::Plain(backdrop_.texture), ui::UiShade::Material, ui::UiBlend::Alpha);
            }
        }
        batch.Quad(part[0], part[1], {0, 0}, {1, 1}, veil, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    }
    const float develop = selfie_ == Selfie::Developing ? 1.0f - Smooth((selfie_time_ - 0.3f) / (kDevelop - 0.5f)) : 0.0f;
    if (develop > 0.0f) {
        const glm::vec4 white(1.0f, 0.985f - 0.05f * (1.0f - develop), 0.96f - 0.12f * (1.0f - develop), develop);
        batch.Quad(wlo, whi, {0, 0}, {1, 1}, white, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    }
}

}
