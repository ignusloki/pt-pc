#include "game/game.h"

#include "engine/core/log.h"
#include "engine/render/scene_lighting.h"
#include "game/lisa_date/lisa_date_audio.h"
#include "game/lisa_date/lisa_date_hints.h"
#include "game/lisa_date/lisa_date_script.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace pt::game {
namespace {

constexpr int kRushesNeeded = kLisaRushesNeeded;
constexpr float kPortraitSeconds = 10.0f;
constexpr glm::vec3 kPink(1.0f, 0.42f, 0.70f);
constexpr uint32_t kCaptionNotes = 0x270D9BACu;
constexpr uint32_t kCaptionHeartbeat = 0xE5067FE7u;
constexpr uint32_t kCaptionSadNote = 0x735D8E0Du;
enum HintSound { kSting = 0, kBeat = 1, kSad = 2 };
constexpr int kSadPending = -1;
constexpr float kVignetteMax = 0.62f;

float Smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

glm::vec3 PinkColour() {
    static const glm::vec3 pink = [] {
        glm::vec3 c = kPink;
        if (const char* v = std::getenv("PT_LISA_HINT_PINK")) std::sscanf(v, "%f,%f,%f", &c.x, &c.y, &c.z);
        return c;
    }();
    return pink;
}

float TintStep(int level) {
    static const std::array<float, kRushesNeeded + 1> steps = [] {
        std::array<float, kRushesNeeded + 1> s{};
        for (int n = 0; n <= kRushesNeeded; ++n) s[n] = LisaHintTintStep(n);
        if (const char* v = std::getenv("PT_LISA_HINT_STEPS")) {
            for (int n = 1; n <= kRushesNeeded && v; ++n) {
                s[n] = static_cast<float>(std::atof(v));
                v = std::strchr(v, ',');
                if (v) ++v;
            }
        }
        return s;
    }();
    return steps[std::clamp(level, 0, kRushesNeeded)];
}

}

void Game::SetLisaRushesForTest(int rushes) {
    rushes = std::clamp(rushes, 0, kRushesNeeded);
    if (GamePlusTier() < 4) {
        LogWarn("lisa date: slisarushes {} ignored below tier 4", rushes);
        return;
    }
    const int before = lisa_rushes_;
    lisa_rushes_ = rushes;
    LogInfo("lisa date: rush count set to {}/{} (input script)", lisa_rushes_, kRushesNeeded);
    if (rushes > before || (rushes == kRushesNeeded && !lisa_hint_lit_)) LisaRushCounted(rushes);
}

void Game::LisaRushCounted(int rush) {
    if (GamePlusTier() < 4 || rush <= 0) return;
    if (rush >= kRushesNeeded) {
        if (!lisa_hint_lit_) LogInfo("lisa date: hint glint lit on the portrait (rush {}/{})", rush, kRushesNeeded);
        lisa_hint_lit_ = true;
    }
    lisa_sting_pending_ = std::min(rush, kRushesNeeded);
    lisa_sting_wait_ = 0.0f;
}

void Game::LisaHintsOff(std::string_view why, bool sad) {
    if (lisa_hint_lit_) {
        LogInfo("lisa date: hint glint and tint fade ({}){}", why, sad ? ", a sad note follows" : "");
        if (sad) {
            lisa_sting_pending_ = kSadPending;
            lisa_sting_wait_ = 0.0f;
        }
    }
    lisa_hint_lit_ = false;
    if (!sad && lisa_sting_pending_ != 0) lisa_sting_pending_ = 0;
}

void Game::LoadLisaHintSounds() {
    if (lisa_hint_sounds_loaded_) return;
    lisa_hint_sounds_loaded_ = true;
    LisaDatePcm music;
    lisa_sting_wav_.assign(kRushesNeeded, {});
    if (LisaDateDecodeMusic(LisaDateDataDir(), "music", music, LisaHintStingSeconds(kRushesNeeded))) {
        for (int n = 1; n <= kRushesNeeded; ++n) {
            lisa_sting_wav_[n - 1] = LisaDateWav(LisaDateExcerpt(music, LisaHintStingSeconds(n), LisaHintStingGain(n), 0.45f));
        }
    }
    lisa_heart_wav_ = LisaDateWav(LisaDateHeartbeat(0.55f));
    lisa_sad_wav_ = LisaDateWav(LisaDateSadNote(0.4f));
}

void Game::PlayLisaHint(int kind, int rush) {
    LoadLisaHintSounds();
    const std::vector<uint8_t>* wav = kind == kSting ? &lisa_sting_wav_[std::clamp(rush, 1, kRushesNeeded) - 1]
                                      : kind == kBeat ? &lisa_heart_wav_
                                                      : &lisa_sad_wav_;
    uint32_t id = 0;
    if (audio_ && !wav->empty()) id = audio_->PlayStream(*wav, nullptr);
    if (kind == kBeat) {
        if (lisa_heart_id_ && audio_) audio_->StopPlayingId(lisa_heart_id_, 0.05f);
        lisa_heart_id_ = id;
    }
    if (kind == kSting) {
        LogInfo("lisa date: hint sting {}/{} (the date's first notes, playing id {})", rush, kRushesNeeded, id);
    } else if (kind == kBeat) {
        LogInfo("lisa date: hint heartbeat {} at {:.1f} s of the zoom (playing id {})", lisa_heart_beats_, lisa_portrait_time_, id);
    } else {
        LogInfo("lisa date: hint sad note (playing id {})", id);
    }
    captions_.Post(kind == kSting ? kCaptionNotes : kind == kBeat ? kCaptionHeartbeat : kCaptionSadNote, std::nullopt);
}

bool Game::LisaHintGlint(glm::vec3& position, glm::vec3& normal, float& strength) const {
    if (lisa_glint_ <= 0.001f || !lisa_glint_valid_) return false;
    position = lisa_glint_at_;
    normal = lisa_glint_normal_;
    float lit = 0.0f;
    if (player_.handy_light.enable && glm::length(handy_direction_) > 0.5f) {
        const glm::vec3 to = position - handy_origin_;
        const float distance = glm::length(to);
        const HandyLightParameters& p = parameters_.handy_light;
        if (distance > 0.01f && distance < p.outer_range) {
            const float c = glm::dot(to / distance, glm::normalize(handy_direction_));
            const float umbra = std::cos(glm::radians(p.umbra_angle * 0.5f));
            const float penumbra = std::cos(glm::radians(p.penumbra_angle * 0.5f));
            lit = Smooth((c - umbra) / std::max(penumbra - umbra, 1.0e-3f));
        }
    }
    const float shimmer = 0.85f + 0.15f * std::sin(lisa_glint_time_ * 2.6f);
    strength = lisa_glint_ * (0.3f + 0.7f * lit) * shimmer;
    return strength > 0.0f;
}

void Game::UpdateLisaHints(float dt) {
    const bool tier = GamePlusTier() >= 4;
    const int level = !tier ? 0 : lisa_rushes_ < kRushesNeeded ? lisa_rushes_ : (lisa_hint_lit_ ? kRushesNeeded : 0);
    if (level != lisa_hint_level_) {
        glm::vec3 colour(1.0f);
        if (level > 0) {
            const HandyLightParameters& p = parameters_.handy_light;
            const glm::vec3 base = light_math::ColorFromTemperature(p.temperature, 0.0f, 1.0f, glm::vec3(p.color[0], p.color[1], p.color[2]));
            const glm::vec3 weights(0.2126f, 0.7152f, 0.0722f);
            const glm::vec3 tint = glm::mix(glm::vec3(1.0f), PinkColour(), TintStep(level));
            colour = tint * (glm::dot(weights, base) / std::max(glm::dot(weights, base * tint), 1.0e-4f));
        }
        LogInfo("lisa date: hint light tint {} -> {} ({:.3f} {:.3f} {:.3f})", lisa_hint_level_, level, colour.r, colour.g, colour.b);
        lisa_hint_level_ = level;
        lisa_hint_colour_ = colour;
    }
    const bool aside = level > 0 && effects_.handy_light_color != glm::vec3(1.0f);
    if (aside != lisa_hint_aside_) {
        lisa_hint_aside_ = aside;
        const glm::vec3& c = effects_.handy_light_color;
        if (aside) LogInfo("lisa date: hint light tint steps aside for the game's own colour ({} {} {})", c.r, c.g, c.b);
        else LogInfo("lisa date: hint light tint back (the game's light is white)");
    }

    const bool glint = tier && lisa_hint_lit_ && floor_.IsCurrentFloorName("f160") && !lisa_date_scene_;
    lisa_glint_ = glint ? std::min(1.0f, lisa_glint_ + dt / 1.5f) : std::max(0.0f, lisa_glint_ - dt / 2.0f);
    lisa_glint_time_ += dt;
    if (lisa_glint_ > 0.0f) {
        glm::vec3 at, normal;
        lisa_glint_valid_ = LisaPortraitPosition(at, &normal);
        if (lisa_glint_valid_) {
            lisa_glint_at_ = at;
            lisa_glint_normal_ = normal;
        }
    } else {
        lisa_glint_valid_ = false;
    }

    if (lisa_sting_pending_ != 0) {
        const bool quiet = objects_.Ocho().State() != 4 && !demos_.ControlsPlayer() && !demos_.HasActiveCamera() && controller_.Step() == 15 &&
                           !paused_ && !lisa_date_scene_;
        lisa_sting_wait_ = quiet ? lisa_sting_wait_ + dt : 0.0f;
        static const bool trace = std::getenv("PT_LISA_HINT_TRACE") != nullptr;
        if (trace && frame_ % 60 == 0) {
            LogInfo("lisa date: hint {} waits: ocho {} demo player {} camera {} step {} paused {}", lisa_sting_pending_, objects_.Ocho().State(),
                    demos_.ControlsPlayer(), demos_.HasActiveCamera(), controller_.Step(), paused_);
        }
        if (lisa_sting_wait_ >= 0.8f) {
            const int pending = std::exchange(lisa_sting_pending_, 0);
            PlayLisaHint(pending == kSadPending ? kSad : kSting, pending);
        }
    }

    lisa_heart_pulse_ = std::max(0.0f, lisa_heart_pulse_ - dt / 0.3f);
    if (lisa_portrait_armed_) {
        const float t = lisa_portrait_time_;
        lisa_heart_wait_ -= dt;
        if (lisa_heart_beats_ == 0 && lisa_heart_wait_ > 0.35f) lisa_heart_wait_ = 0.35f;
        if (lisa_heart_wait_ <= 0.0f) {
            ++lisa_heart_beats_;
            PlayLisaHint(kBeat, 0);
            lisa_heart_wait_ += 1.05f - 0.6f * Smooth(t / kPortraitSeconds);
            lisa_heart_pulse_ = 1.0f;
        }
        const float x = std::clamp(t / kPortraitSeconds, 0.0f, 1.0f);
        lisa_vignette_ = kVignetteMax * std::pow(x, 1.3f) * (1.0f + 0.15f * lisa_heart_pulse_);
    } else {
        if (lisa_heart_beats_ > 0) {
            if (lisa_heart_id_ && audio_) audio_->StopPlayingId(lisa_heart_id_, 0.4f);
            LogInfo("lisa date: hint heartbeat and pink edges fade ({} beats)", lisa_heart_beats_);
        }
        lisa_heart_id_ = 0;
        lisa_heart_beats_ = 0;
        lisa_heart_wait_ = 0.0f;
        lisa_vignette_ = lisa_date_scene_ ? 0.0f : std::max(0.0f, lisa_vignette_ - dt * kVignetteMax / 0.7f);
    }
}

}
