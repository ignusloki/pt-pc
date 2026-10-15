#include "game/game.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "engine/core/log.h"
#include "engine/render/model_cache.h"
#include "game/lisa_date/lisa_date_audio.h"

namespace pt::game {
namespace {

using intercom::Play;

constexpr const char* kMonitor = "shsb_mntr001_0000";
constexpr const char* kPainting = "shsb_pctr001_vrtn003_0007";
constexpr const char* kFrame = "shsb_hous001_pict015_0000";
constexpr const char* kArea = "LA_SL_monitor";
constexpr const char* kLitModel = "/Assets/sh/environ/object/shsb/monitor/shsb_mntr001/scenes/shsb_mntr001_vrtn001.fmdl";
const glm::vec4 kScreen(1.5f, 1.6037f, -13.127f, 1.0f);
const glm::vec4 kLightAt(1.5f, 1.6037f, -13.157f, 1.0f);
constexpr const char* kDonorEvent = "Play_sfx_door_open_01";
constexpr uint32_t kCaptionChime = 0x9E69FB14u;
constexpr uint32_t kCaptionHum = 0x568431F2u;
constexpr uint32_t kCaptionCrackle = 0x44859513u;
constexpr float kTellLead = 4.0f;
struct Gains {
    float chime = 0.095f;
    float hum = 0.065f;
    float crackle = 0.075f;
};

const Gains& SoundGains() {
    static const Gains gains = [] {
        Gains g;
        if (const char* v = std::getenv("PT_INTERCOM_GAIN")) std::sscanf(v, "%f,%f,%f", &g.chime, &g.hum, &g.crackle);
        return g;
    }();
    return gains;
}

bool Inside(const glm::mat4& box, const glm::vec3& point) {
    const glm::vec3 local(glm::inverse(box) * glm::vec4(point, 1.0f));
    return std::abs(local.x) <= 1.0f && std::abs(local.y) <= 1.0f && std::abs(local.z) <= 1.0f;
}

const char* PhaseName(Play::Phase phase) {
    switch (phase) {
        case Play::Phase::Chime: return "chime";
        case Play::Phase::Lit: return "lit";
        case Play::Phase::Crackle: return "crackle";
        default: return "idle";
    }
}

}

bool Game::SwapIntercom(Stage& stage, bool show, Play::Site& site) {
    bool found = false;
    for (const auto& file : stage.files) {
        const fox2::DataSetFile& data = *file->file;
        const fox2::Entity* monitor = data.ByShortName(kMonitor);
        if (!monitor) continue;
        found = true;
        auto data_visible = [&](const fox2::Entity& e) { return (data.GetUInt(e, "flags", 0, 7) & 1) != 0; };
        stage.Body(monitor).visible = show || data_visible(*monitor);
        for (const char* name : {kPainting, kFrame}) {
            if (const fox2::Entity* e = data.ByShortName(name)) stage.Body(e).visible = !show && data_visible(*e);
        }
        site.monitor = stage.ToWorld(data.WorldTransform(*monitor));
        if (const fox2::Entity* area = data.ByShortName(kArea)) {
            site.area = stage.ToWorld(data.WorldTransform(*area)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
            site.has_area = std::abs(glm::determinant(glm::mat3(site.area))) > 1.0e-9f;
        }
        break;
    }
    if (found) {
        stages_.MarkVisualsDirty();
        const glm::vec3 at(site.monitor * kScreen);
        LogInfo("game+: intercom {} in {} ({}), screen at ({:.2f} {:.2f} {:.2f}){}", show ? "shown, painting hidden" : "hidden, painting back",
                stage.label, floor_.CurrentFloorName(), at.x, at.y, at.z, site.has_area ? "" : ", no light area");
    }
    return found;
}

int Game::IntercomShown() const {
    int shown = 0;
    for (const Play::Site& site : intercom_.sites) {
        const Stage* stage = const_cast<StageManager&>(stages_).FindById(site.stage);
        shown += site.found && stage && stage->active ? 1 : 0;
    }
    return shown;
}

const Play::Site* Game::IntercomSite(bool inside_only) const {
    const glm::vec3 at = player_.Feet() + glm::vec3(0.0f, 0.5f, 0.0f);
    const Play::Site* best = nullptr;
    float best_distance = 1e30f;
    for (const Play::Site& site : intercom_.sites) {
        const Stage* stage = const_cast<StageManager&>(stages_).FindById(site.stage);
        if (!site.found || !stage || !stage->active) continue;
        if (inside_only) {
            if (site.has_area && Inside(site.area, at)) return &site;
            continue;
        }
        const float d = glm::distance(glm::vec3(site.monitor * kScreen), at);
        if (d < best_distance) {
            best_distance = d;
            best = &site;
        }
    }
    return best;
}

void Game::StartIntercom(Play::Phase phase, const Play::Site& site, std::string_view why) {
    Play& p = intercom_;
    if (p.phase != Play::Phase::Idle) StopIntercom("a new one starts");
    p.phase = phase;
    p.time = 0.0f;
    p.site_stage = site.stage;
    p.monitor = site.monitor;
    p.area = site.area;
    p.has_area = site.has_area;
    p.seq_seed = intercom::Mix(p.run_seed, static_cast<uint32_t>(frame_));
    p.level = 0.0f;
    if (!p.model_ready) p.model_ready = models_.Get(kLitModel) != nullptr;
    const glm::vec3 at(site.monitor * kScreen);
    const float distance = glm::distance(at, player_.Feet() + glm::vec3(0.0f, 1.5f, 0.0f));
    if (phase == Play::Phase::Chime) {
        p.curve = intercom::RingCurve(p.seq_seed, &p.lit_seconds);
        if (audio_) p.chime_id = audio_->PlayStreamAs(LisaDateWav(intercom::ChimePcm(SoundGains().chime)), kDonorEvent, at);
        LogInfo("game+: intercom rings ({}; {} pass {}, frame {}), {:.1f} m from the player, lit for {:.1f} s", why, floor_.CurrentFloorName(),
                floor_.LoopCount(), frame_, distance, p.lit_seconds);
        captions_.Post(kCaptionChime, at);
    } else {
        p.curve = intercom::CrackleCurve(p.seq_seed);
        if (audio_) {
            p.crackle_id = audio_->PlayStreamAs(LisaDateWav(intercom::CracklePcm(p.curve, p.seq_seed, SoundGains().crackle)), kDonorEvent, at);
        }
        LogInfo("game+: intercom crackles ({}; {} pass {}, frame {}), {:.1f} m from the player", why, floor_.CurrentFloorName(), floor_.LoopCount(),
                frame_, distance);
        captions_.Post(kCaptionCrackle, at);
    }
}

void Game::StopIntercom(std::string_view why) {
    Play& p = intercom_;
    if (p.phase == Play::Phase::Idle) return;
    for (uint32_t* id : {&p.chime_id, &p.hum_id, &p.crackle_id}) {
        if (*id && audio_) audio_->StopPlayingId(*id, 0.15f);
        *id = 0;
    }
    LogInfo("game+: intercom stops in its {} ({}, frame {})", PhaseName(p.phase), why, frame_);
    p.phase = Play::Phase::Idle;
    p.level = 0.0f;
    p.ring_after = false;
}

void Game::IntercomScript(std::string_view what) {
    Play& p = intercom_;
    if (what == "ring" || what == "never" || what == "auto") {
        p.force = what == "ring" ? 1 : what == "never" ? 0 : -1;
        p.rolled = false;
        LogInfo("game+: intercom rings {} (input script)", what == "ring" ? "every loop" : what == "never" ? "never" : "as rolled");
        return;
    }
    if (GamePlusTier() < kTierIntercom) {
        LogWarn("game+: sintercom {} ignored below tier {}", what, kTierIntercom);
        return;
    }
    const Play::Site* site = IntercomSite(false);
    if (!site) {
        LogWarn("game+: sintercom {}: no intercom loaded", what);
        return;
    }
    if (what == "now") {
        StartIntercom(Play::Phase::Chime, *site, "input script");
        p.rung = true;
    } else if (what == "crackle") {
        StartIntercom(Play::Phase::Crackle, *site, "input script");
    } else {
        LogWarn("game+: sintercom {} not understood", what);
    }
}

bool Game::GamePlusIntercomLight(glm::vec3& position, glm::vec3& direction, glm::mat4& area, bool& has_area, float& level) const {
    const Play& p = intercom_;
    if (p.phase == Play::Phase::Idle || p.phase == Play::Phase::Chime || p.level <= 0.002f) return false;
    position = glm::vec3(p.monitor * kLightAt);
    direction = glm::normalize(glm::mat3(p.monitor) * glm::vec3(0.0f, 0.0f, -1.0f));
    area = p.area;
    has_area = p.has_area;
    level = p.level;
    return true;
}

void Game::AddIntercomDraw(std::vector<DrawItem>& out) const {
    const Play& p = intercom_;
    if (p.phase == Play::Phase::Idle || p.level <= 0.002f || !p.model_ready) return;
    const ModelEntry* model = const_cast<ModelCache&>(models_).Get(kLitModel);
    if (!model || !model->mesh) return;
    DrawItem item;
    item.mesh = model->mesh.get();
    item.transform = p.monitor;
    item.tint = glm::vec4(1.0f, 1.0f, 1.0f, std::clamp(p.level, 0.0f, 1.0f));
    for (size_t i = 0; i < model->mesh->submeshes.size() && i < kMaxHiddenMeshes; ++i) {
        item.hidden_meshes[i] = model->mesh->submeshes[i].kind != gpu::kKindConstant;
    }
    item.source = 0x494E54434F4D0000ull ^ p.site_stage;
    out.push_back(item);
}

void Game::UpdateGamePlusIntercom(float dt) {
    Play& p = intercom_;
    if (p.run_seed == 0) {
        const char* seed = std::getenv("PT_INTERCOM_SEED");
        p.run_seed = seed && *seed ? static_cast<uint32_t>(std::strtoul(seed, nullptr, 10)) : intercom::Mix(config_.seed ? config_.seed : ReadTsc(), 0x1C0Fu);
        if (p.run_seed == 0) p.run_seed = 1;
        if (const char* ring = std::getenv("PT_INTERCOM_RING"); ring && *ring) p.force = std::atoi(ring) != 0 ? 1 : 0;
    }
    const bool on = GamePlusTier() >= kTierIntercom;
    std::vector<uint32_t> live;
    stages_.ForEachStage([&](Stage& stage) {
        live.push_back(stage.id);
        auto it = std::find_if(p.sites.begin(), p.sites.end(), [&](const Play::Site& s) { return s.stage == stage.id; });
        if (on && it == p.sites.end()) {
            Play::Site site;
            site.stage = stage.id;
            site.found = SwapIntercom(stage, true, site);
            p.sites.push_back(site);
            if (site.found && !p.model_ready) p.model_ready = models_.Get(kLitModel) != nullptr;
        } else if (!on && it != p.sites.end()) {
            if (it->found) SwapIntercom(stage, false, *it);
            p.sites.erase(it);
        }
    });
    std::erase_if(p.sites, [&](const Play::Site& s) { return std::find(live.begin(), live.end(), s.stage) == live.end(); });
    if (!on) {
        StopIntercom("Game+ below tier 2");
        return;
    }
    p.clock += dt;
    const int step = controller_.Step();
    const std::string& floor = floor_.CurrentFloorName();
    const int pass = floor_.LoopCount();
    if (floor != p.floor || pass != p.pass) {
        p.floor = floor;
        p.pass = pass;
        p.rolled = false;
        p.rung = false;
        p.chase_told = false;
    }
    const bool play = step == 15 && !street_walk_;

    OchoLogic& ocho = objects_.Ocho();
    if (ocho.State() == 1 && ocho.WarpPhase() == 2 && play) {
        const float left = ocho.Wait() - ocho.TimerA();
        if (!p.warp_told && left <= kTellLead && left > 0.0f) {
            p.warp_told = true;
            if (const Play::Site* site = IntercomSite(false)) {
                p.told_at = p.clock;
                if (p.phase == Play::Phase::Idle) {
                    StartIntercom(Play::Phase::Crackle, *site, "Lisa's tell");
                } else if (audio_) {
                    const glm::vec3 at(site->monitor * kScreen);
                    audio_->PlayStreamAs(LisaDateWav(intercom::CracklePcm(intercom::CrackleCurve(p.seq_seed), p.seq_seed, SoundGains().crackle)),
                                         kDonorEvent, at);
                    captions_.Post(kCaptionCrackle, at);
                }
                LogInfo("game+: intercom tell for Lisa's herald, {:.2f} s before it ({} pass {}, frame {})", left, floor, pass, frame_);
            }
        }
    }
    if (ocho.State() == 1 && p.warp_phase == 2 && ocho.WarpPhase() == 0) {
        if (p.warp_told && p.told_at >= 0.0) {
            LogInfo("game+: intercom: Lisa's herald {:.2f} s after the tell (frame {})", p.clock - p.told_at, frame_);
        }
        p.warp_told = false;
    }
    if (ocho.State() != 1) p.warp_told = false;
    p.warp_phase = ocho.WarpPhase();
    if ((ocho.State() == 2 || ocho.State() == 3) && p.ocho_state != ocho.State()) {
        if (p.chase_pending && p.told_at >= 0.0) {
            LogInfo("game+: intercom: Lisa's chase (state {}) began {:.2f} s after the tell ({} pass {})", ocho.State(), p.clock - p.told_at, floor, pass);
        }
        p.chase_pending = false;
    }
    p.ocho_state = ocho.State();

    const Play::Site* inside_site = IntercomSite(true);
    const bool inside = inside_site != nullptr;
    if (play && inside && !p.inside && !demos_.ControlsPlayer()) {
        if (!p.rolled) {
            p.rolled = true;
            const float chance = intercom::RingChance(floor);
            p.ring = p.force >= 0 ? p.force == 1 : intercom::RingRoll(p.run_seed, floor, pass, chance);
            LogInfo("game+: intercom roll for {} pass {}: {} ({}{:.0f}%)", floor, pass, p.ring ? "rings" : "silent", p.force >= 0 ? "forced, " : "",
                    chance * 100.0f);
        }
        const bool chase_next = floor == "f060" || (floor == "f050" && !nazo_.IsCleared(NazoId::XMark));
        if (chase_next && !p.chase_told) {
            p.chase_told = true;
            p.chase_pending = true;
            p.told_at = p.clock;
            const bool ring = p.ring && !p.rung;
            if (p.phase == Play::Phase::Idle) StartIntercom(Play::Phase::Crackle, *inside_site, "Lisa's tell, her chase next");
            p.ring_after = ring;
            if (ring) p.rung = true;
            LogInfo("game+: intercom tell for Lisa's chase on the next loop ({} pass {}, frame {})", floor, pass, frame_);
        } else if (p.ring && !p.rung && p.phase == Play::Phase::Idle) {
            p.rung = true;
            StartIntercom(Play::Phase::Chime, *inside_site, "the player at the corridor's end");
        }
    }
    p.inside = inside;

    if (p.phase == Play::Phase::Idle) return;
    const Stage* stage = stages_.FindById(p.site_stage);
    if (!stage || !stage->active || !play) {
        StopIntercom(!stage || !stage->active ? "its stage left" : "play stopped");
        return;
    }
    p.time += dt;
    if (p.phase == Play::Phase::Chime) {
        p.level = 0.0f;
        if (p.time >= intercom::kChimeSeconds) {
            p.phase = Play::Phase::Lit;
            p.time = 0.0f;
            const glm::vec3 at(p.monitor * kScreen);
            if (audio_) p.hum_id = audio_->PlayStreamAs(LisaDateWav(intercom::HumPcm(p.curve, p.seq_seed, SoundGains().hum)), kDonorEvent, at);
            LogInfo("game+: intercom screen sputters on (frame {})", frame_);
            captions_.Post(kCaptionHum, at);
        }
        return;
    }
    p.level = p.curve.Level(p.time);
    static const bool trace = std::getenv("PT_INTERCOM_TRACE") != nullptr;
    if (trace) LogInfo("game+: intercom trace frame {} {} t {:.3f} level {:.3f}", frame_, PhaseName(p.phase), p.time, p.level);
    if (p.time >= p.curve.Duration() + (p.phase == Play::Phase::Crackle ? 0.3f : 0.0f)) {
        LogInfo("game+: intercom {} over, the screen grey again (frame {})", PhaseName(p.phase), frame_);
        const bool ring_after = p.ring_after;
        p.phase = Play::Phase::Idle;
        p.level = 0.0f;
        p.chime_id = p.hum_id = p.crackle_id = 0;
        p.ring_after = false;
        if (ring_after) {
            Play::Site site;
            site.stage = p.site_stage;
            site.found = true;
            site.monitor = p.monitor;
            site.area = p.area;
            site.has_area = p.has_area;
            StartIntercom(Play::Phase::Chime, site, "after the tell");
        }
    }
}

}
