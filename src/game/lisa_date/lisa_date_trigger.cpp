#include "game/game.h"

#include "engine/core/log.h"
#include "game/lisa_date/lisa_date_hints.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace pt::game {
namespace {

constexpr int kRushesNeeded = kLisaRushesNeeded;
constexpr float kPortraitSeconds = 10.0f;
constexpr float kPortraitReach = 3.5f;
constexpr float kPortraitArea = 0.6f;
constexpr float kPortraitLeaveArea = 0.8f;
constexpr float kPortraitLeaveReach = 4.0f;
constexpr float kPortraitLeaveSeconds = 0.3f;
constexpr const char* kPortraitEntity = "shsb_hous001_ches001_0000";
const glm::vec4 kPortraitMiddle(0.0755f, 1.113f, -23.557f, 1.0f);

}

void Game::OnLisaRush(std::string_view how) {
    if (GamePlusTier() < 4 || !floor_.IsCurrentFloorName("f160")) return;
    if (lisa_rush_counted_) {
        LogInfo("lisa date: {} in the same f160 loop, rush count stays {}/{}", how, lisa_rushes_, kRushesNeeded);
        return;
    }
    lisa_rush_counted_ = true;
    if (lisa_rushes_ >= kRushesNeeded) {
        LogInfo("lisa date: {} on f160, rush count stays {}/{} (the portrait is open)", how, lisa_rushes_, kRushesNeeded);
        if (!lisa_hint_lit_) LisaRushCounted(lisa_rushes_);
        return;
    }
    ++lisa_rushes_;
    LogInfo("lisa date: rush {}/{} ({} on f160 loop {})", lisa_rushes_, kRushesNeeded, how, floor_.LoopCount());
    LisaRushCounted(lisa_rushes_);
}

void Game::LisaDateFloorEntered(std::string_view from, std::string_view floor) {
    lisa_rush_counted_ = false;
    lisa_portrait_time_ = 0.0f;
    if (from == "f160") LisaHintsOff(std::format("NextFloor {} -> {}", from, floor), true);
    if (floor == "f160") return;
    if (lisa_rushes_ > 0) LogInfo("lisa date: rush count cleared ({} entered)", floor);
    lisa_rushes_ = 0;
}

bool Game::LisaPortraitPosition(glm::vec3& out, glm::vec3* normal) const {
    const glm::vec3 eye = player_.Eye();
    bool found = false;
    float best = 1e30f;
    const_cast<StageManager&>(stages_).ForEachStage([&](Stage& stage) {
        if (!stage.active) return;
        for (const Stage::Draw& draw : stage.draws) {
            if (!draw.entity || !draw.file || !draw.file->file->EntityName(*draw.entity).ends_with(kPortraitEntity)) continue;
            static const glm::vec4 middle = [] {
                glm::vec4 m = kPortraitMiddle;
                if (const char* v = std::getenv("PT_LISA_DATE_PORTRAIT")) std::sscanf(v, "%f,%f,%f", &m.x, &m.y, &m.z);
                return m;
            }();
            const glm::mat4 world = stage.ToWorld(draw.file_transform);
            const glm::vec3 at(world * middle);
            const float d = glm::distance(at, eye);
            if (d < best) {
                best = d;
                out = at;
                found = true;
                if (normal) {
                    const glm::vec3 z = glm::normalize(glm::vec3(world[2]));
                    *normal = glm::dot(eye - at, z) >= 0.0f ? z : -z;
                }
            }
        }
    });
    return found;
}

bool Game::UpdateLisaPortrait(float dt) {
    if (GamePlusTier() < 4 || lisa_rushes_ < kRushesNeeded || lisa_date_scene_ || !floor_.IsCurrentFloorName("f160")) {
        lisa_portrait_time_ = 0.0f;
        lisa_portrait_armed_ = false;
        lisa_portrait_lost_ = 0.0f;
        return false;
    }
    if (lisa_portrait_release_) {
        if ((player_.HeldButtons() & kPadZoom) != 0) return false;
        lisa_portrait_release_ = false;
    }
    const bool deliberate = !nazo_.IsActionArmed() && player_.Standing() && (player_.HeldButtons() & kPadZoom) != 0;
    glm::vec3 portrait;
    bool found = deliberate && LisaPortraitPosition(portrait);
    bool inside = false;
    if (found) {
        const glm::vec3 eye = player_.Eye();
        const glm::vec3 to = portrait - eye;
        const float distance = glm::length(to);
        const bool armed = lisa_portrait_armed_;
        inside = distance < (armed ? kPortraitLeaveReach : kPortraitReach) && InViewHeldZoom(portrait, armed ? kPortraitLeaveArea : kPortraitArea);
        RayHit hit;
        if (inside && distance > 0.1f && line_collision_.Raycast(eye, to / distance, distance - 0.08f, hit)) inside = false;
    }
    if (!deliberate || !found) {
        if (lisa_portrait_armed_) LogInfo("lisa date: portrait zoom released after {:.1f} s", lisa_portrait_time_);
        lisa_portrait_time_ = 0.0f;
        lisa_portrait_armed_ = false;
        lisa_portrait_lost_ = 0.0f;
        return false;
    }
    if (inside) {
        lisa_portrait_lost_ = 0.0f;
    } else if (lisa_portrait_armed_ && lisa_portrait_lost_ + dt < kPortraitLeaveSeconds) {
        lisa_portrait_lost_ += dt;
    } else {
        if (lisa_portrait_armed_) LogInfo("lisa date: portrait left the view after {:.1f} s", lisa_portrait_time_);
        lisa_portrait_time_ = 0.0f;
        lisa_portrait_armed_ = false;
        lisa_portrait_lost_ = 0.0f;
        return false;
    }
    if (!lisa_portrait_armed_) {
        lisa_portrait_armed_ = true;
        LogInfo("lisa date: portrait zoom armed (the wedding photo at {:.2f} {:.2f} {:.2f})", portrait.x, portrait.y, portrait.z);
    }
    lisa_portrait_time_ += dt;
    if (lisa_portrait_time_ >= kPortraitSeconds) {
        lisa_portrait_time_ = 0.0f;
        lisa_portrait_armed_ = false;
        lisa_portrait_lost_ = 0.0f;
        lisa_portrait_release_ = true;
        LisaHintsOff("the date started", false);
        if (lisa_heart_id_ && audio_) audio_->StopPlayingId(lisa_heart_id_, 0.3f);
        lisa_heart_id_ = 0;
        lisa_heart_beats_ = 0;
        lisa_vignette_ = 0.0f;
        RequestLisaDate("the portrait zoom");
        LogInfo("lisa date: portrait held {:.0f} s, the date starts", kPortraitSeconds);
    }
    return true;
}

bool Game::InViewHeldZoom(const glm::vec3& position, float area) const {
    const Camera camera = player_.MakeCamera();
    const glm::vec3 offset = position - camera.position;
    const float depth = glm::dot(offset, camera.Forward());
    if (!(depth > 0.0f && depth < camera.far_plane)) return false;
    const float tan_half = std::tan(camera.fov_y * 0.5f) * player_.zoom / Player::kZoomHeld;
    const float ndc_y = glm::dot(offset, camera.Up()) / (depth * tan_half);
    const float ndc_x = glm::dot(offset, camera.Right()) / (depth * tan_half * (16.0f / 9.0f));
    return std::abs(ndc_x) < area && std::abs(ndc_y) < area;
}

void Game::HoldSaves(bool hold) {
    if (hold == saves_held_) return;
    saves_held_ = hold;
    if (hold) return;
    const bool full = std::exchange(save_deferred_, false);
    const bool options = std::exchange(options_save_deferred_, false);
    if (full) {
        LogInfo("save: the save held during the date is made now");
        RequestSave();
    } else if (options) {
        RequestOptionsSave();
    }
}

}
