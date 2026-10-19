#include "game/lisa_date/lisa_date.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>

#include "engine/audio/sound_package.h"
#include "engine/core/log.h"
#include "engine/fs/vfs.h"
#include "engine/render/model_cache.h"
#include "engine/render/scene_lighting.h"
#include "engine/render/texture_manager.h"
#include "engine/ui/text_layout.h"
#include "game/game.h"
#include "game/game_plus_arm.h"
#include "game/lisa_date/lisa_date_audio.h"
#include "game/ui/ui_assets.h"
#include "engine/platform/os.h"

#include "stb_image.h"

namespace pt::game {
namespace {

constexpr const char* kLisaModel = "/Assets/sh/chara/och/Scenes/och0_main0_def.fmdl";
constexpr const char* kTubLisaModel = "/Assets/sh/environ/object/shsb/bath/shsb_bath001/scenes/shsb_bath001_ocho001.fmdl";
constexpr const char* kHallwayPackage = "/Assets/sh/level/promotion/pt_2014/hallway/pt14_hallway.fpk";
constexpr const char* kKillDemo = "/Assets/sh/demo/demo_stream/gc_p07_030.fsm";

constexpr float kIntroCover = 1.4f;
constexpr float kIntroLength = 3.2f;
const glm::vec3 kEye(0.0f, 0.30f, 2.10f);
const glm::vec3 kNormalTarget(0.0f, 0.27f, 0.0f);
constexpr float kNormalFov = 30.0f;
constexpr float kModelScale = 0.9f;
constexpr float kBackdropFocus = 0.375f;
constexpr float kTableShift = 0.0f;
constexpr float kOutroBlack = 0.8f;
constexpr float kOutroLength = 1.6f;
constexpr float kTypeRate = 42.0f;

const glm::vec4 kPink(1.0f, 0.78f, 0.86f, 1.0f);
const glm::vec4 kLavender(0.82f, 0.76f, 0.98f, 1.0f);
const glm::vec4 kPeach(1.0f, 0.86f, 0.74f, 1.0f);
const glm::vec4 kRose(0.93f, 0.45f, 0.62f, 1.0f);
const glm::vec4 kInk(0.30f, 0.16f, 0.22f, 1.0f);

float Ease(float current, float target, float rate, float dt) {
    return target + (current - target) * std::exp(-rate * dt);
}

glm::vec3 Ease(const glm::vec3& current, const glm::vec3& target, float rate, float dt) {
    return target + (current - target) * std::exp(-rate * dt);
}

float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::mat4 About(const glm::vec3& pivot, const glm::mat4& rotation) {
    return glm::translate(glm::mat4(1.0f), pivot) * rotation * glm::translate(glm::mat4(1.0f), -pivot);
}

size_t CharLength(const std::string& s, size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
}

size_t CountChars(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i += CharLength(s, i)) ++n;
    return n;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

struct NamedLook {
    const char* name;
    float lean, pitch, roll, yaw, smile, jaw, lids, zoom;
    glm::vec3 tint;
};
const NamedLook kLooks[] = {
    {"neutral", 1.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.1f, 1.0f, {1.0f, 1.0f, 1.0f}},
    {"happy", 3.0f, -3.0f, 4.0f, 0.0f, 1.0f, 0.35f, 0.3f, 1.0f, {1.06f, 1.0f, 0.92f}},
    {"shy", 5.0f, 9.0f, -7.0f, 8.0f, 0.55f, 0.0f, 0.5f, 1.05f, {1.08f, 0.84f, 0.9f}},
    {"sad", 1.0f, 12.0f, 4.0f, -4.0f, -0.8f, 0.1f, 0.45f, 1.0f, {0.84f, 0.88f, 1.05f}},
    {"angry", -3.0f, 6.0f, 0.0f, 0.0f, -1.0f, 0.2f, 0.4f, 1.08f, {1.12f, 0.68f, 0.62f}},
    {"creepy", 8.0f, -2.0f, 7.0f, 0.0f, 0.65f, 0.0f, -0.6f, 1.12f, {0.85f, 0.85f, 0.95f}},
    {"smile", 3.0f, -3.0f, 4.0f, 0.0f, 1.0f, 0.35f, 0.3f, 1.0f, {1.06f, 1.0f, 0.92f}},
    {"blush", 5.0f, 9.0f, -7.0f, 8.0f, 0.55f, 0.0f, 0.5f, 1.05f, {1.08f, 0.84f, 0.9f}},
    {"stare", 8.0f, -2.0f, 7.0f, 0.0f, 0.65f, 0.0f, -0.6f, 1.12f, {0.85f, 0.85f, 0.95f}},
    {"tilt", 1.0f, 2.0f, 16.0f, 0.0f, 0.2f, 0.0f, 0.15f, 1.0f, {1.0f, 1.0f, 1.0f}},
    {"lean", 11.0f, -3.0f, 3.0f, 0.0f, 0.4f, 0.0f, 0.2f, 1.04f, {1.0f, 1.0f, 1.0f}},
};

}

LisaDate::LisaDate(Vfs& vfs, ModelCache& models, UiAssets& assets, GameAudio* audio) : vfs_(vfs), models_(models), assets_(assets), audio_(audio) {}

LisaDate::~LisaDate() {
    StopMusic(0.0f);
    if (audio_) {
        for (uint32_t id : {bed_id_, scare_bed_id_, herald_id_}) {
            if (id) audio_->StopPlayingId(id, 0.5f);
        }
    }
    if (audio_ && scare_id_) audio_->StopPlayingId(scare_id_, 0.0f);
}

bool LisaDate::Start(const Settings& settings) {
    settings_ = settings;
    dir_ = LisaDateDataDir();
    std::string error;
    if (!script_.Parse(ReadText(dir_ / "script.txt"), &error)) {
        LogError("lisa date: {} ({})", error.empty() ? "no script.txt" : error, pt::os::PathToUtf8(dir_ / "script.txt"));
        return false;
    }
    LoadText();
    run_ = std::make_unique<LisaDateRun>(script_);
    run_->Restart();
    const ModelEntry* lisa = models_.Get(kLisaModel);
    if (!lisa || !lisa->mesh) {
        LogError("lisa date: {} not loaded", kLisaModel);
        return false;
    }
    lisa_mesh_ = settings.lisa_mesh ? settings.lisa_mesh : lisa->mesh.get();
    lisa_hidden_ = settings.lisa_hidden;
    player_mesh_ = settings.player_mesh;
    player_hidden_ = settings.player_hidden;
    player_help_ = settings.player_help;
    bone_names_ = lisa->bone_names;
    bind_ = lisa->bone_bind_world;
    parents_.assign(lisa->bone_parents.begin(), lisa->bone_parents.end());
    BuildPose();
    LoadMusic();
    table_ = LoadPicture("fg_table");
    if (!table_.texture) table_ = LoadPicture("table");
    logo_ = LoadPicture("logo");
    polaroid_ = LoadPicture("polaroid");
    phase_ = Phase::Intro;
    phase_time_ = settings.replay ? kIntroCover - 0.3f : 0.0f;
    light_ = light_target_;
    shot_ = shot_target_ = {kNormalTarget, 32.0f};
    LogInfo("lisa date: started ({}), {} commands, {} choices, text {} English keys and {} of language {}, pose {}", settings.cause,
            script_.Commands().size(), script_.Choices().size(), text_.EnglishCount(), text_.LanguageCount(), settings.language, pose_source_);
    return true;
}

LisaDate::Picture LisaDate::LoadPicture(const std::string& name) {
    Picture p;
    for (const char* ext : {".png", ".jpg"}) {
        const std::filesystem::path path = dir_ / (name + ext);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        int w = 0, h = 0, n = 0;
        if (stbi_info(pt::os::PathToUtf8(path).c_str(), &w, &h, &n) && h > 0) p.aspect = static_cast<float>(w) / static_cast<float>(h);
        p.texture = assets_.LocalPreview(pt::os::PathToUtf8(path));
        if (p.texture) LogInfo("lisa date: picture {}{} {}x{}", name, ext, w, h);
        return p;
    }
    return p;
}

void LisaDate::LoadText() {
    text_.SetEnglish(LisaDateScript::ParseText(ReadText(dir_ / "text" / "eng.txt")));
    const int language = std::clamp(settings_.language, 0, UiAssets::kLanguageCount - 1);
    if (language > 0) {
        text_.SetLanguage(LisaDateScript::ParseText(ReadText(dir_ / "text" / (std::string(UiAssets::kLanguageCodes[language]) + ".txt"))));
    }
}

void LisaDate::LoadMusic() {
    auto pcm = std::make_shared<LisaDatePcm>();
    if (!LisaDateDecodeMusic(dir_, "music", *pcm)) return;
    music_loop_ = LisaMusicLoopFor(*pcm);
    const double rate = static_cast<double>(pcm->rate);
    if (music_loop_.Valid()) {
        LogInfo("lisa date: music loop {:.3f} s to {:.3f} s, crossfade {:.1f} s, outro to {:.3f} s", music_loop_.a / rate, music_loop_.b / rate,
                music_loop_.crossfade / rate, static_cast<double>(pcm->samples.size() / pcm->channels) / rate);
    } else {
        LogInfo("lisa date: music too short for its loop ({:.1f} s), played whole and posted again", static_cast<double>(pcm->samples.size() / pcm->channels) / rate);
    }
    music_ = std::move(pcm);
}

void LisaDate::PlayMusic(const std::string& name) {
    if (name == music_name_ && music_id_ && audio_ && audio_->IsPlaying(music_id_)) return;
    StopMusic(0.4f);
    music_name_ = name;
    if (!audio_ || !music_) return;
    const bool slow = name == "slow";
    if (slow && !music_slow_) {
        music_slow_ = std::make_shared<const LisaDatePcm>(LisaDateSlowed(*music_, 0.82f));
        music_slow_loop_ = LisaMusicLoopFor(*music_slow_, 0.82);
    }
    const std::shared_ptr<const LisaDatePcm>& pcm = slow ? music_slow_ : music_;
    music_playing_loop_ = slow ? music_slow_loop_ : music_loop_;
    music_playing_frames_ = pcm->samples.size() / pcm->channels;
    music_playing_rate_ = pcm->rate;
    music_state_ = std::make_shared<LisaMusicState>();
    music_state_->release.store(music_ending_);
    music_seams_logged_ = 0;
    music_outro_logged_ = music_end_logged_ = false;
    music_id_ = audio_->PlayMedia(LisaMusicMedia(pcm, music_playing_loop_, music_state_), nullptr);
    LogInfo("lisa date: music {} (playing id {})", name, music_id_);
}

void LisaDate::StopMusic(float fade) {
    if (audio_ && music_id_) audio_->StopPlayingId(music_id_, fade);
    music_id_ = 0;
    music_name_.clear();
    music_state_.reset();
}

void LisaDate::ReleaseMusic() {
    music_ending_ = true;
    if (!music_state_ || !music_id_) return;
    music_state_->release.store(true);
    const uint64_t pos = music_state_->position.load();
    const LisaMusicLoop& loop = music_playing_loop_;
    const bool crossing = loop.Valid() && pos >= loop.b - loop.crossfade && pos < loop.b;
    const double rate = static_cast<double>(std::max(music_playing_rate_, 1u));
    LogInfo("lisa date: music released at {:.3f} s of the file, to its end in {:.1f} s", pos / rate,
            LisaMusicPlayer::FramesToEnd(loop, music_playing_frames_, pos, crossing) / rate);
}

bool LisaDate::MusicPlayingOut() const {
    return music_ending_ && music_id_ && music_state_ && !music_state_->ended.load() && audio_ && audio_->IsPlaying(music_id_);
}

int LisaDate::Mood() const {
    if (scare_mood_) return 2;
    const bool creepy = expression_ == "creepy" || expression_ == "stare";
    if (creepy || light_name_ == "dark" || light_name_ == "glitch" || tear_ > 0.0f || flicker_ || glitch_ > 0.0f) return 1;
    return 0;
}

void LisaDate::SetMood(int mood) {
    if (mood == mood_) return;
    LogInfo("lisa date: sound {} -> {}", mood_ == 0 ? "sweet" : mood_ == 1 ? "dread" : "scare", mood == 0 ? "sweet" : mood == 1 ? "dread" : "scare");
    if (mood != 0 && music_id_) StopMusic(1.5f);
    if (mood != 1 && bed_id_) {
        audio_->StopPlayingId(bed_id_, 1.5f);
        bed_id_ = 0;
    }
    if (mood != 2 && scare_bed_id_) {
        audio_->StopPlayingId(scare_bed_id_, 1.5f);
        scare_bed_id_ = 0;
    }
    mood_ = mood;
    mood_since_ = time_;
}

void LisaDate::UpdateMusic() {
    if (!audio_ || phase_ == Phase::Outro || phase_ == Phase::Done) return;
    if (music_state_) {
        const double rate = static_cast<double>(std::max(music_playing_rate_, 1u));
        const uint32_t seams = music_state_->seams.load();
        if (seams != music_seams_logged_) {
            music_seams_logged_ = seams;
            LogInfo("lisa date: music seam {} (B {:.3f} s to A {:.3f} s)", seams, music_playing_loop_.b / rate, music_playing_loop_.a / rate);
        }
        if (music_state_->outro.load() && !music_outro_logged_) {
            music_outro_logged_ = true;
            LogInfo("lisa date: music outro, the pass runs past B {:.3f} s to the end", music_playing_loop_.b / rate);
        }
        if (music_state_->ended.load() && !music_end_logged_) {
            music_end_logged_ = true;
            LogInfo("lisa date: music ended ({:.3f} s, the end of its file)", music_playing_frames_ / rate);
        }
    }
    const int want = Mood();
    if (want > mood_ || (want < mood_ && time_ - mood_since_ > 4.0f)) SetMood(want);
    auto alive = [&](uint32_t id) { return id != 0 && audio_->IsPlaying(id); };
    if (mood_ == 0) {
        if (music_wanted_.empty()) {
            if (music_id_) StopMusic(1.2f);
        } else if (music_name_ != music_wanted_ || (!alive(music_id_) && !(music_ending_ && music_state_ && music_state_->ended.load()))) {
            music_name_.clear();
            music_id_ = 0;
            PlayMusic(music_wanted_);
        }
    } else if (mood_ == 1 && !alive(bed_id_) && !bed_failed_) {
        bed_id_ = audio_->PostEventId(0x1A95368Du, nullptr);
        bed_failed_ = bed_id_ == 0;
        LogInfo("lisa date: the ambient bed 0x1A95368D (playing id {})", bed_id_);
    } else if (mood_ == 2 && !alive(scare_bed_id_) && !scare_bed_failed_) {
        scare_bed_id_ = audio_->PostEvent("Play_bgm_unrest01", nullptr);
        scare_bed_failed_ = scare_bed_id_ == 0;
        LogInfo("lisa date: Play_bgm_unrest01 (playing id {})", scare_bed_id_);
    }
}

void LisaDate::PlayScare() {
    if (!audio_) return;
    const auto bytes = vfs_.ReadFile(kKillDemo);
    if (!bytes) {
        LogWarn("lisa date: {} not readable, the scare is silent", kKillDemo);
        return;
    }
    if (auto stream = audio::ExtractDemoStreamAudio(*bytes)) {
        scare_id_ = audio_->PlayStream(std::move(stream->wem), nullptr);
        LogInfo("lisa date: the kill demo's sound (playing id {})", scare_id_);
    }
}

void LisaDate::BuildPose() {
    static std::vector<glm::mat4> cached;
    static bool tried = false;
    if (!tried) {
        tried = true;
        bool mounted_here = false;
        if (!vfs_.ReadFile(kTubLisaModel)) {
            mounted_here = vfs_.LoadPackage(kHallwayPackage) != nullptr;
            LogInfo("lisa date: {} read for the sitting pose{}", kHallwayPackage, mounted_here ? "" : " (not found)");
        }
        if (vfs_.ReadFile(kTubLisaModel) && models_.Get(kTubLisaModel)) {
            LisaPoseFit fit = FitLisaStaticPose(models_, kTubLisaModel, {}, 0.5, 0.05, "the bathtub Lisa");
            if (fit.ok) cached = std::move(fit.skin);
        }
        if (mounted_here) vfs_.UnloadPackage(kHallwayPackage);
    }
    auto find = [&](std::string_view name) {
        for (size_t i = 0; i < bone_names_.size(); ++i) {
            if (bone_names_[i] == name) return static_cast<int>(i);
        }
        return -1;
    };
    const int waist = find("SKL_000_WAIST"), chest = find("SKL_002_CHEST"), neck = find("SKL_003_NECK"), head = find("SKL_004_HEAD");
    (void)head;
    if (cached.size() != bind_.size() || waist < 0 || chest < 0 || neck < 0 || head < 0) {
        BuildFallbackPose();
        return;
    }
    std::vector<glm::mat4> skin = cached;
    auto joint = [&](const std::vector<glm::mat4>& s, int b) { return glm::vec3(s[static_cast<size_t>(b)] * glm::vec4(bind_[static_cast<size_t>(b)], 1.0f)); };
    auto under = [&](size_t b, int root) {
        for (int p = static_cast<int>(b); p >= 0; p = parents_[static_cast<size_t>(p)]) {
            if (p == root) return true;
        }
        return false;
    };
    for (size_t b = 0; b < skin.size(); ++b) {
        if (static_cast<int>(b) != neck && under(b, neck)) skin[b] = skin[static_cast<size_t>(neck)];
    }
    const glm::vec3 w = joint(skin, waist);
    const glm::vec3 up = glm::normalize(joint(skin, neck) - w);
    glm::vec3 facing = glm::mat3(skin[static_cast<size_t>(chest)]) * glm::vec3(0.0f, 0.0f, 1.0f);
    facing = glm::normalize(facing - up * glm::dot(facing, up));
    const glm::mat3 basis(glm::cross(up, facing), up, facing);
    const glm::mat4 seat = glm::mat4(glm::transpose(basis)) * glm::translate(glm::mat4(1.0f), -w);
    for (glm::mat4& m : skin) m = seat * m;
    for (const char* name : {"SKL_001_SPINE", "SKL_002_CHEST", "SKL_003_NECK", "SKL_004_HEAD", "SKL_010_LSHLD", "SKL_020_RSHLD"}) {
        const int b = find(name);
        if (b < 0) continue;
        const int parent = parents_[static_cast<size_t>(b)];
        const glm::mat4 old_world = skin[static_cast<size_t>(b)] * glm::translate(glm::mat4(1.0f), bind_[static_cast<size_t>(b)]);
        glm::vec3 at = glm::vec3(old_world[3]);
        if (parent >= 0) {
            const glm::mat4 parent_world = skin[static_cast<size_t>(parent)] * glm::translate(glm::mat4(1.0f), bind_[static_cast<size_t>(parent)]);
            at = glm::vec3(parent_world * glm::vec4(bind_[static_cast<size_t>(b)] - bind_[static_cast<size_t>(parent)], 1.0f));
            if (std::string_view(name) == "SKL_001_SPINE") at = glm::vec3(parent_world[3]) + (bind_[static_cast<size_t>(b)] - bind_[static_cast<size_t>(parent)]);
        }
        const glm::mat4 new_world = glm::translate(glm::mat4(1.0f), at);
        const glm::mat4 delta = new_world * glm::inverse(old_world);
        for (size_t k = 0; k < skin.size(); ++k) {
            if (static_cast<int>(k) != b && under(k, b)) skin[k] = delta * skin[k];
        }
        skin[static_cast<size_t>(b)] = new_world * glm::translate(glm::mat4(1.0f), -bind_[static_cast<size_t>(b)]);
    }
    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(kModelScale));
    for (glm::mat4& m : skin) m = scale * m;
    const float recline = glm::degrees(std::acos(std::clamp(glm::dot(up, glm::vec3(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f)));
    seated_ = std::move(skin);
    pose_source_ = std::format("the bathtub Lisa's fit (trunk turned {:.0f} degrees upright; spine, neck, head and shoulders set straight)", recline);
}

void LisaDate::BuildFallbackPose() {
    const size_t n = bind_.size();
    std::vector<glm::quat> local(n, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    for (size_t b = 0; b < n; ++b) {
        const std::string& name = bone_names_[b];
        if (name.ends_with("_LTHIGH") || name.ends_with("_RTHIGH")) local[b] = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));
        if (name.ends_with("_LLEG") || name.ends_with("_RLEG")) local[b] = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));
        if (name.ends_with("_LUARM")) local[b] = glm::angleAxis(glm::radians(-78.0f), glm::vec3(0, 0, 1));
        if (name.ends_with("_RUARM")) local[b] = glm::angleAxis(glm::radians(78.0f), glm::vec3(0, 0, 1));
    }
    std::vector<glm::quat> rotation(n);
    std::vector<glm::vec3> position(n);
    seated_.assign(n, glm::mat4(1.0f));
    for (size_t b = 0; b < n; ++b) {
        const int p = b < parents_.size() ? parents_[b] : -1;
        if (p < 0) {
            rotation[b] = local[b];
            position[b] = glm::vec3(0.0f);
        } else {
            rotation[b] = rotation[static_cast<size_t>(p)] * local[b];
            position[b] = position[static_cast<size_t>(p)] + rotation[static_cast<size_t>(p)] * (bind_[b] - bind_[static_cast<size_t>(p)]);
        }
        seated_[b] = glm::translate(glm::mat4(1.0f), position[b]) * glm::mat4_cast(rotation[b]) * glm::translate(glm::mat4(1.0f), -bind_[b]);
    }
    pose_source_ = "the built-in sitting pose (no bathtub model loaded)";
}

void LisaDate::PoseFrame() {
    skin_ = seated_;
    if (skin_.empty()) return;
    auto find = [&](std::string_view name) {
        for (size_t i = 0; i < bone_names_.size(); ++i) {
            if (bone_names_[i] == name) return static_cast<int>(i);
        }
        return -1;
    };
    struct Bones {
        int spine, chest, neck, head, lshld, rshld, lid_top, lid_low, mouth_l, mouth_r, mouth_dl, mouth_dr, mouth_tl, mouth_tr, cheek_r, cheek_l,
            cheek_ro, cheek_lo;
    };
    static const std::vector<std::string>* names = nullptr;
    static Bones k{};
    if (names != &bone_names_) {
        names = &bone_names_;
        k = {find("SKL_001_SPINE"),      find("SKL_002_CHEST"),       find("SKL_003_NECK"),        find("SKL_004_HEAD"),
             find("SKL_010_LSHLD"),      find("SKL_020_RSHLD"),       find("SKL_405_EYE_TOP"),     find("SKL_401_EYE_DOWN"),
             find("SKL_411_MOUTH_L"),    find("SKL_410_MOUTH_R"),     find("SKL_408_MOUTH_DOWN_L"), find("SKL_409_MOUTH_DOWN_R"),
             find("SKL_407_MOUTH_TOP_L"), find("SKL_406_MOUTH_TOP_R"),  find("SKL_412_FACE"),         find("SKL_413_FACE"),
             find("SKL_414_FACE"),        find("SKL_415_FACE")};
    }
    auto joint = [&](int b) { return glm::vec3(skin_[static_cast<size_t>(b)] * glm::vec4(bind_[static_cast<size_t>(b)], 1.0f)); };
    auto under = [&](size_t b, int root) {
        for (int p = static_cast<int>(b); p >= 0; p = parents_[static_cast<size_t>(p)]) {
            if (p == root) return true;
        }
        return false;
    };
    auto turn = [&](int root, const glm::mat4& rotation) {
        if (root < 0) return;
        const glm::mat4 m = About(joint(root), rotation);
        for (size_t b = 0; b < skin_.size(); ++b) {
            if (under(b, root)) skin_[b] = m * skin_[b];
        }
    };
    auto nudge = [&](int b, glm::vec3 offset) {
        if (b < 0 || k.head < 0) return;
        const glm::vec3 d = glm::mat3(skin_[static_cast<size_t>(k.head)]) * offset;
        skin_[static_cast<size_t>(b)] = glm::translate(glm::mat4(1.0f), d) * skin_[static_cast<size_t>(b)];
    };
    const float time = frozen_ ? frozen_time_ : time_;
    const Look& look = frozen_ ? photo_look_ : look_;
    const bool creepy = !frozen_ && (expression_ == "creepy" || expression_ == "stare");
    const float breath = std::sin(time * 6.2831853f / (creepy ? 6.5f : 4.2f)) * (creepy ? 0.4f : 1.0f);
    const float sway = std::sin(time * 6.2831853f / 7.3f) * (creepy ? 0.2f : 1.0f);
    turn(k.spine, glm::rotate(glm::mat4(1.0f), glm::radians(look.lean + 0.5f * breath), glm::vec3(1, 0, 0)) *
                      glm::rotate(glm::mat4(1.0f), glm::radians(0.8f * sway), glm::vec3(0, 0, 1)));
    turn(k.chest, glm::rotate(glm::mat4(1.0f), glm::radians(-0.9f * breath), glm::vec3(1, 0, 0)));
    float pitch = look.pitch, roll = look.roll, yaw = look.yaw;
    glm::vec2 shoulders(0.0f);
    if (twitch_time_ > 0.0f && !frozen_) {
        const float step = std::floor(twitch_time_ / 0.06f);
        std::mt19937 jerk(static_cast<uint32_t>(step) * 2654435761u + 7u);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        const float a = twitch_amp_ * std::min(1.0f, twitch_time_ / 0.15f + 0.3f);
        roll += 22.0f * a * d(jerk);
        yaw += 12.0f * a * d(jerk);
        pitch += 9.0f * a * d(jerk);
        shoulders = glm::vec2(d(jerk), d(jerk)) * (5.0f * a);
    }
    pitch += 1.0f * std::sin(time * 6.2831853f / 5.1f + 1.0f);
    if (creepy) roll += 4.0f * std::sin(time * 0.35f);
    if (k.lshld >= 0) turn(k.lshld, glm::rotate(glm::mat4(1.0f), glm::radians(shoulders.x + 0.4f * breath), glm::vec3(0, 0, 1)));
    if (k.rshld >= 0) turn(k.rshld, glm::rotate(glm::mat4(1.0f), glm::radians(-shoulders.y - 0.4f * breath), glm::vec3(0, 0, 1)));
    turn(k.neck, glm::rotate(glm::mat4(1.0f), glm::radians(yaw * 0.4f), glm::vec3(0, 1, 0)) *
                     glm::rotate(glm::mat4(1.0f), glm::radians(pitch * 0.4f), glm::vec3(1, 0, 0)) *
                     glm::rotate(glm::mat4(1.0f), glm::radians(roll * 0.4f), glm::vec3(0, 0, 1)));
    turn(k.head, glm::rotate(glm::mat4(1.0f), glm::radians(yaw * 0.6f), glm::vec3(0, 1, 0)) *
                     glm::rotate(glm::mat4(1.0f), glm::radians(pitch * 0.6f), glm::vec3(1, 0, 0)) *
                     glm::rotate(glm::mat4(1.0f), glm::radians(roll * 0.6f), glm::vec3(0, 0, 1)));
    const bool talking = !frozen_ && phase_ == Phase::Scene && wait_ == Wait::Line && speaker_ == "lisa" && typed_ < static_cast<float>(line_chars_);
    const float jaw = std::clamp(look.jaw + (talking ? 0.8f * std::abs(std::sin(time_ * 17.0f)) : 0.0f), 0.0f, 1.2f);
    nudge(k.mouth_dl, {0.0f, -0.008f * jaw, 0.0f});
    nudge(k.mouth_dr, {0.0f, -0.008f * jaw, 0.0f});
    nudge(k.mouth_tl, {0.0f, 0.0015f * jaw, 0.0f});
    nudge(k.mouth_tr, {0.0f, 0.0015f * jaw, 0.0f});
    nudge(k.mouth_l, {0.003f * look.smile, 0.004f * look.smile, 0.0f});
    nudge(k.mouth_r, {-0.003f * look.smile, 0.004f * look.smile, 0.0f});
    if (selfie_ != Selfie::Off && k.cheek_r >= 0 && k.cheek_l >= 0) {
        const float lift = std::max(0.0f, look.smile);
        nudge(k.cheek_r, {-0.0010f * lift, 0.0030f * lift, 0.0010f * lift});
        nudge(k.cheek_l, {0.0010f * lift, 0.0030f * lift, 0.0010f * lift});
        nudge(k.cheek_ro, {0.0f, 0.0020f * lift, 0.0f});
        nudge(k.cheek_lo, {0.0f, 0.0020f * lift, 0.0f});
    }
    float lids = look.lids;
    if (blink_time_ > 0.0f && !frozen_) lids = std::max(lids, 1.0f - std::abs(blink_time_ - 0.075f) / 0.075f);
    if (lids >= 0.0f) {
        nudge(k.lid_top, {0.0f, -0.018f * std::min(lids, 1.0f), 0.002f * lids});
        nudge(k.lid_low, {0.0f, 0.003f * std::min(lids, 1.0f), 0.0f});
    } else {
        nudge(k.lid_top, {0.0f, 0.004f * -lids, 0.0f});
        nudge(k.lid_low, {0.0f, -0.003f * -lids, 0.0f});
    }
    const glm::mat4 bob = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.003f * breath, 0.0f));
    for (glm::mat4& m : skin_) m = bob * m;
}

void LisaDate::Twitch(float length, float amp) {
    twitch_time_ = length;
    twitch_length_ = length;
    twitch_amp_ = amp;
}

void LisaDate::CollectDraws(std::vector<DrawItem>& out) {
    if (!showing_scene_ || !lisa_mesh_ || skin_.empty()) return;
    DrawItem item;
    item.mesh = lisa_mesh_;
    item.transform = glm::mat4(1.0f);
    item.skin = skin_;
    item.hidden_meshes = lisa_hidden_;
    item.source = 0x4C49534144415445ull;
    out.push_back(item);
    if (selfie_ != Selfie::Off && player_mesh_ && !player_skin_.empty()) {
        DrawItem him;
        him.mesh = player_mesh_;
        him.transform = player_transform_;
        him.skin = player_skin_;
        him.hidden_meshes = player_hidden_;
        him.source = 0x53454C4649450000ull;
        out.push_back(him);
    }
}

Camera LisaDate::ViewCamera() const {
    Camera c;
    glm::vec3 position = kEye;
    glm::vec3 target = shot_.target;
    float fov = shot_.fov;
    float zoom = look_.zoom;
    if (selfie_camera_) {
        position = selfie_eye_;
        target = selfie_target_;
        fov = selfie_fov_;
        zoom = 1.0f;
    }
    static const std::optional<std::pair<glm::vec3, glm::vec3>> forced = []() -> std::optional<std::pair<glm::vec3, glm::vec3>> {
        const char* v = std::getenv("PT_LISA_DATE_CAMERA");
        glm::vec3 a, b;
        if (v && std::sscanf(v, "%f,%f,%f,%f,%f,%f", &a.x, &a.y, &a.z, &b.x, &b.y, &b.z) == 6) return std::pair(a, b);
        return std::nullopt;
    }();
    if (forced) {
        position = forced->first;
        target = forced->second;
    }
    if (shake_time_ > 0.0f) {
        const float a = 0.02f * std::min(1.0f, shake_time_ * 2.0f);
        target += glm::vec3(a * std::sin(time_ * 71.0f), a * std::sin(time_ * 53.0f + 1.0f), 0.0f);
    }
    const glm::vec3 d = glm::normalize(target - position);
    c.position = position;
    c.yaw = std::atan2(-d.x, -d.z);
    c.pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    c.roll = 0.0f;
    c.fov_y = 2.0f * std::atan(std::tan(glm::radians(fov) * 0.5f) / std::max(zoom, 0.5f));
    c.near_plane = 0.05f;
    c.far_plane = 100.0f;
    return c;
}

void LisaDate::ApplyLighting(SceneLighting& lighting) const {
    lighting = SceneLighting{};
    lighting.valid = true;
    struct Rig {
        glm::vec3 position;
        glm::vec3 color;
    };
    float flicker = 1.0f;
    if (light_name_ == "glitch") flicker = 0.6f + 0.4f * (std::sin(time_ * 37.0f) > 0.2f ? 1.0f : 0.3f);
    const Rig rig[] = {
        {{-1.1f, 1.4f, 1.7f}, light_.key * 7.0f * flicker},
        {{1.5f, 0.6f, 1.3f}, light_.fill * 3.2f},
        {{0.5f, 1.5f, -1.3f}, light_.rim * 4.5f},
    };
    for (size_t i = 0; i < 3; ++i) {
        SceneLight light;
        light.id = 0x4C44415445000000ull + i;
        light.name = "LisaDate";
        light.position = rig[i].position;
        light.intensity = rig[i].color * (frozen_ ? photo_look_.tint : look_.tint) * (1.0f - 0.85f * light_.darken);
        light.inner_range = 0.2f;
        light.outer_range = 8.0f;
        light.source_radius = 0.3f;
        light.specular_scale = 0.4f;
        lighting.lights.push_back(light);
    }
    lighting.exposure.pinned = true;
    lighting.exposure.pinned_ev = 0.0f;
    lighting.exposure.compensation = 0.0f;
    for (float& compensation : lighting.exposure.add_comp) compensation = 0.0f;
    lighting.exposure.bloom_weight = 0.0f;
    lighting.screen.color_scale = glm::vec3(1.0f);
    lighting.screen.film_grain = false;
    lighting.screen.screen_distortion = false;
    lighting.screen.depth_of_field = false;
    lighting.screen.motion_blur = false;
    lighting.screen.local_reflections = false;
}

SceneRenderer::Backdrop LisaDate::Backdrop() const {
    SceneRenderer::Backdrop b;
    if (!showing_scene_ || !backdrop_.texture) return b;
    b.texture = backdrop_.texture;
    if (time_ < flicker_until_ && flicker_picture_.texture) b.texture = flicker_picture_.texture;
    b.opacity = 1.0f;
    b.focus = kBackdropFocus;
    const Camera now = ViewCamera();
    const glm::vec3 f0 = glm::normalize(kNormalTarget - kEye);
    const glm::vec3 r0 = glm::normalize(glm::cross(f0, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 u0 = glm::cross(r0, f0);
    const glm::vec3 f = now.Forward();
    const float t0 = std::tan(glm::radians(kNormalFov) * 0.5f);
    const float depth = std::max(glm::dot(f, f0), 0.2f);
    b.center = glm::vec2(glm::dot(f, r0) / depth / t0, glm::dot(f, u0) / depth / t0);
    b.zoom = t0 / std::tan(now.fov_y * 0.5f);
    b.darken = 0.6f * light_.darken;
    b.tear = std::max(tear_, glitch_ * 0.35f);
    b.time = time_;
    b.tint = light_.tint;
    b.aspect = backdrop_.aspect;
    return b;
}

void LisaDate::Update(float dt, const InputState& input) {
    time_ += dt;
    prompts_ = input.prompts;
    phase_time_ += dt;
    if (backdrop_.texture == 0) backdrop_ = LoadPicture("bg_" + backdrop_name_);
    look_.lean = Ease(look_.lean, look_target_.lean, 4.0f, dt);
    look_.pitch = Ease(look_.pitch, look_target_.pitch, 4.0f, dt);
    look_.roll = Ease(look_.roll, look_target_.roll, 4.0f, dt);
    look_.yaw = Ease(look_.yaw, look_target_.yaw, 4.0f, dt);
    look_.smile = Ease(look_.smile, look_target_.smile, 6.0f, dt);
    look_.jaw = Ease(look_.jaw, look_target_.jaw, 6.0f, dt);
    look_.lids = Ease(look_.lids, look_target_.lids, 6.0f, dt);
    look_.zoom = Ease(look_.zoom, look_target_.zoom, 1.5f, dt);
    look_.tint = Ease(look_.tint, look_target_.tint, 2.0f, dt);
    blink_time_ = std::max(0.0f, blink_time_ - dt);
    if (time_ >= next_blink_) {
        const bool creepy = expression_ == "creepy" || expression_ == "stare";
        std::uniform_real_distribution<float> gap(creepy ? 8.0f : 2.5f, creepy ? 12.0f : 5.5f);
        blink_time_ = 0.15f;
        next_blink_ = time_ + gap(rng_);
    }
    if (twitch_mode_ > 0 && phase_ == Phase::Scene && time_ >= next_twitch_) {
        std::uniform_real_distribution<float> gap(twitch_mode_ == 1 ? 30.0f : 4.0f, twitch_mode_ == 1 ? 60.0f : 9.0f);
        next_twitch_ = time_ + gap(rng_);
        if (twitch_time_ <= 0.0f) Twitch(twitch_mode_ == 1 ? 0.35f : 0.5f, twitch_mode_ == 1 ? 0.55f : 0.9f);
    }
    if (flicker_ && time_ >= next_flicker_) {
        std::uniform_real_distribution<float> gap(2.0f, 5.0f);
        next_flicker_ = time_ + gap(rng_);
        flicker_until_ = time_ + (rng_() % 2 ? 0.034f : 0.017f);
        glitch_ = std::max(glitch_, 0.0f);
    }
    light_.key = Ease(light_.key, light_target_.key, 2.2f, dt);
    light_.fill = Ease(light_.fill, light_target_.fill, 2.2f, dt);
    light_.rim = Ease(light_.rim, light_target_.rim, 2.2f, dt);
    light_.tint = Ease(light_.tint, light_target_.tint, 2.2f, dt);
    light_.darken = Ease(light_.darken, light_target_.darken, 2.2f, dt);
    shot_.target = Ease(shot_.target, shot_target_.target, shot_rate_, dt);
    shot_.fov = Ease(shot_.fov, shot_target_.fov, shot_rate_, dt);
    twitch_time_ = std::max(0.0f, twitch_time_ - dt);
    shake_time_ = std::max(0.0f, shake_time_ - dt);
    flash_time_ = std::max(0.0f, flash_time_ - dt);
    UpdateMusic();

    if (phase_ == Phase::Intro) {
        showing_scene_ = phase_time_ >= kIntroCover;
        if (phase_time_ >= kIntroLength) {
            phase_ = Phase::Scene;
            phase_time_ = 0.0f;
        }
    } else if (phase_ == Phase::Outro) {
        showing_scene_ = phase_time_ < kOutroBlack;
        if (!showing_scene_) StopMusic(0.0f);
        if (phase_time_ >= kOutroLength) {
            phase_ = Phase::Done;
            backdrop_ = {};
            LogInfo("lisa date: finished ({})", ending_.empty() ? "no ending" : ending_);
        }
    }
    PoseFrame();
    PosePlayer();
    photo_note_time_ = std::max(0.0f, photo_note_time_ - dt);
    if (phase_ != Phase::Scene) return;

    pointer_valid_ = input.pointer_valid && canvas_ready_;
    if (pointer_valid_) pointer_ = (input.pointer - canvas_.origin) / canvas_.scale;
    const bool up = (input.raw_pressed & kRawUp) != 0;
    const bool down = (input.raw_pressed & kRawDown) != 0;
    bool stick_up = false, stick_down = false;
    if (std::abs(input.left_stick.y) > 0.6f) {
        if (!stick_held_) (input.left_stick.y > 0.0f ? stick_up : stick_down) = true;
        stick_held_ = true;
    } else {
        stick_held_ = false;
    }
    const bool left = (input.raw_pressed & kRawLeft) != 0;
    const bool right = (input.raw_pressed & kRawRight) != 0;
    bool stick_left = false, stick_right = false;
    if (std::abs(input.left_stick.x) > 0.6f) {
        if (!stick_held_x_) (input.left_stick.x > 0.0f ? stick_right : stick_left) = true;
        stick_held_x_ = true;
    } else {
        stick_held_x_ = false;
    }
    leave_hold_ = (input.raw_held & kRawCircle) ? leave_hold_ + dt : 0.0f;
    if (leave_hold_ >= 2.0f) {
        leave_hold_ = 0.0f;
        Leave();
        return;
    }
    const bool accept = (input.raw_pressed & kRawCross) != 0 && !input.click;
    bool clicked = input.click;
    const bool pointer_moved = pointer_valid_ && glm::distance(pointer_, last_pointer_) > 0.5f;
    last_pointer_ = pointer_;
    if (wait_ == Wait::Selfie) {
        UpdateSelfie(dt, input, up || stick_up, down || stick_down, left || stick_left, right || stick_right, accept, clicked, pointer_moved);
        return;
    }
    if ((input.raw_pressed & kRawTriangle) || input.date_auto_pressed || (clicked && pointer_valid_ && auto_button_.Contains(pointer_))) {
        auto_ = !auto_;
        skip_ = false;
        clicked = false;
        LogInfo("lisa date: auto {}", auto_ ? "on" : "off");
    }
    if ((input.raw_pressed & kRawSquare) || input.date_skip_pressed || (clicked && pointer_valid_ && skip_button_.Contains(pointer_))) {
        skip_ = !skip_;
        auto_ = false;
        clicked = false;
        LogInfo("lisa date: skip {}", skip_ ? "on" : "off");
    }

    if (block_time_ > 0.0f) {
        block_time_ -= dt;
        if (scare_time_ >= 0.0f) {
            scare_time_ += dt;
            if (scare_time_ > 2.4f && audio_ && scare_id_) {
                audio_->StopPlayingId(scare_id_, 0.6f);
                scare_id_ = 0;
            }
        }
        if (block_time_ > 0.0f) return;
        block_time_ = 0.0f;
        scare_time_ = -1.0f;
        wait_ = Wait::None;
        if (pending_stop_ >= 0) {
            const int stop = pending_stop_;
            pending_stop_ = -1;
            HandleStop(stop);
        }
    }
    if (wait_ == Wait::None) AdvanceScript();

    switch (wait_) {
    case Wait::Line: {
        const float rate = skip_ ? 600.0f : kTypeRate;
        typed_ = std::min(static_cast<float>(line_chars_), typed_ + rate * dt);
        const bool complete = typed_ >= static_cast<float>(line_chars_);
        if (complete) shown_time_ += dt;
        static const bool trace = std::getenv("PT_LISA_DATE_TRACE") != nullptr;
        if (accept || clicked) {
            if (trace) LogInfo("lisa date trace: {} by {}", line_key_, accept ? "cross" : "click");
            if (complete) Accept();
            else FinishLine();
        } else if (complete && skip_ && shown_time_ > 0.05f) {
            if (trace) LogInfo("lisa date trace: {} by skip", line_key_);
            Accept();
        } else if (complete && auto_ && shown_time_ > 1.1f + 0.035f * static_cast<float>(line_chars_)) {
            if (trace) LogInfo("lisa date trace: {} by auto", line_key_);
            Accept();
        }
        break;
    }
    case Wait::Choice: {
        skip_ = false;
        const LisaDateChoice* choice = run_->CurrentChoice();
        const int count = choice ? static_cast<int>(choice->options.size()) : 0;
        if (count == 0) break;
        if (up || stick_up) selected_ = (selected_ + count - 1) % count;
        if (down || stick_down) selected_ = (selected_ + 1) % count;
        int hovered = -1;
        for (size_t i = 0; i < choice_buttons_.size(); ++i) {
            if (pointer_valid_ && choice_buttons_[i].Contains(pointer_)) hovered = static_cast<int>(i);
        }
        if (hovered >= 0 && pointer_moved) selected_ = hovered;
        if (hovered >= 0 && clicked) Choose(hovered);
        else if (accept) Choose(selected_);
        break;
    }
    case Wait::Timer:
        wait_time_ -= dt;
        if (wait_time_ <= 0.0f) Accept();
        break;
    case Wait::Title:
        wait_time_ -= dt;
        if (wait_time_ <= 0.0f || ((accept || clicked || skip_) && wait_time_ < 1.8f)) Accept();
        break;
    case Wait::Ending:
        wait_time_ -= dt;
        if ((wait_time_ <= 0.0f && (skip_ || !MusicPlayingOut())) || ((accept || clicked) && wait_time_ < 3.0f)) StartOutro();
        break;
    default:
        break;
    }
}

void LisaDate::AdvanceScript() {
    std::vector<LisaDateRun::Directive> directives;
    const LisaDateRun::Stop stop = run_->Advance(directives);
    for (const auto& d : directives) ApplyDirective(d);
    if (block_time_ > 0.0f) {
        pending_stop_ = static_cast<int>(stop);
        wait_ = Wait::Block;
        return;
    }
    HandleStop(static_cast<int>(stop));
}

void LisaDate::HandleStop(int stop_value) {
    const auto stop = static_cast<LisaDateRun::Stop>(stop_value);
    const LisaDateCommand* c = run_->Current();
    switch (stop) {
    case LisaDateRun::Stop::Line:
        if (!c->expr.empty()) ApplyDirective({LisaDateCommand::Op::Expr, c->expr});
        SetLine(c->a, c->b);
        break;
    case LisaDateRun::Stop::Choice: {
        wait_ = Wait::Choice;
        selected_ = 0;
        choice_buttons_.clear();
        std::string keys;
        for (const auto& o : run_->CurrentChoice()->options) keys += " " + o.key;
        LogInfo("lisa date: choice {} up:{}", c->a, keys);
        break;
    }
    case LisaDateRun::Stop::Wait:
        wait_ = Wait::Timer;
        wait_time_ = c->number;
        break;
    case LisaDateRun::Stop::Title:
        wait_ = Wait::Title;
        wait_time_ = 2.4f;
        title_ = text_.Get(c->b);
        LogInfo("lisa date: scene {} ({})", c->a, c->b);
        break;
    case LisaDateRun::Stop::Ending:
        ending_ = std::string(run_->EndingName());
        ending_title_ = text_.Get("ending_" + ending_);
        wait_ = Wait::Ending;
        wait_time_ = 4.5f;
        skip_ = auto_ = false;
        LogInfo("lisa date: ending {} (affection {}, flags {}, {} choices)", ending_, run_->Affection(), run_->Secrets(), run_->ChoicesMade());
        ReleaseMusic();
        break;
    case LisaDateRun::Stop::Selfie:
        StartSelfie();
        break;
    case LisaDateRun::Stop::Done:
        StartOutro();
        break;
    }
}

void LisaDate::ApplyDirective(const LisaDateRun::Directive& d) {
    using Op = LisaDateCommand::Op;
    const std::string& n = d.name;
    switch (d.op) {
    case Op::Bg:
        if (n != backdrop_name_) {
            backdrop_name_ = n;
            backdrop_ = {};
            LogInfo("lisa date: backdrop {}", n);
        }
        break;
    case Op::Music:
        if (n == "stop") music_wanted_.clear();
        else music_wanted_ = n;
        break;
    case Op::Expr:
        expression_ = n;
        {
            bool known = n == "twitch";
            for (const NamedLook& l : kLooks) {
                if (n == l.name) {
                    look_target_ = {l.lean, l.pitch, l.roll, l.yaw, l.smile, l.jaw, l.lids, l.zoom, l.tint};
                    known = true;
                }
            }
            if (n == "twitch") Twitch(0.4f, 0.6f);
            if (!known) LogWarn("lisa date: unknown expression {}", n);
        }
        break;
    case Op::Light: {
        light_name_ = n;
        LightSet s;
        if (n == "warm") s = {{1.0f, 0.74f, 0.52f}, {1.0f, 0.72f, 0.7f}, {1.0f, 0.6f, 0.3f}, {1.0f, 1.0f, 1.0f}, 0.0f};
        else if (n == "blush") s = {{1.0f, 0.62f, 0.7f}, {1.0f, 0.5f, 0.75f}, {1.0f, 0.55f, 0.5f}, {1.0f, 0.9f, 0.94f}, 0.0f};
        else if (n == "angry") s = {{1.0f, 0.28f, 0.22f}, {0.5f, 0.15f, 0.2f}, {1.0f, 0.15f, 0.1f}, {1.0f, 0.55f, 0.5f}, 0.15f};
        else if (n == "glitch") s = {{0.6f, 0.9f, 1.0f}, {1.0f, 0.3f, 0.9f}, {0.3f, 1.0f, 0.8f}, {0.8f, 0.85f, 1.0f}, 0.25f};
        else if (n == "dark") s = {{0.25f, 0.06f, 0.06f}, {0.05f, 0.02f, 0.04f}, {0.7f, 0.08f, 0.05f}, {0.5f, 0.3f, 0.3f}, 0.8f};
        else if (n == "dusk") s = {{0.85f, 0.55f, 0.48f}, {0.6f, 0.45f, 0.95f}, {0.95f, 0.45f, 0.3f}, {0.8f, 0.72f, 0.88f}, 0.1f};
        else LogWarn("lisa date: unknown light {}", n);
        light_target_ = s;
        break;
    }
    case Op::Cam:
        shot_rate_ = 2.5f;
        if (n == "normal") shot_target_ = {kNormalTarget, kNormalFov};
        else if (n == "far") shot_target_ = {kNormalTarget, 32.0f};
        else if (n == "close") shot_target_ = {{0.0f, 0.43f, 0.0f}, 20.0f};
        else if (n == "face") shot_target_ = {{0.0f, 0.57f, 0.0f}, 11.0f};
        else if (n == "rush") {
            shot_target_ = {{0.0f, 0.59f, 0.0f}, 5.0f};
            shot_rate_ = 14.0f;
        } else LogWarn("lisa date: unknown camera {}", n);
        break;
    case Op::Fx:
        if (n == "flash") {
            flash_time_ = 0.35f;
            flash_color_ = glm::vec3(1.0f);
        } else if (n == "heart") {
            flash_time_ = 0.6f;
            flash_color_ = glm::vec3(1.0f, 0.7f, 0.82f);
        } else if (n == "shake") {
            shake_time_ = 0.6f;
        } else if (n == "glitch") {
            glitch_ = 1.0f;
        } else if (n == "noglitch") {
            glitch_ = 0.0f;
        } else if (n == "tear") {
            tear_ = 1.0f;
            glitch_ = 1.0f;
        } else if (n == "notear") {
            tear_ = 0.0f;
            glitch_ = 0.0f;
        } else if (n == "twitch") {
            Twitch(0.4f, 0.6f);
        } else if (n == "twitch_violent") {
            Twitch(1.1f, 2.0f);
            block_time_ = std::max(block_time_, 1.1f);
            scare_mood_ = true;
            if (audio_ && !herald_id_) herald_id_ = audio_->PostEventId(0xD567DD28u, nullptr);
            LogInfo("lisa date: violent twitch");
        } else if (n == "twitch_rare" || n == "twitch_often" || n == "twitch_off") {
            twitch_mode_ = n == "twitch_rare" ? 1 : n == "twitch_often" ? 2 : 0;
            std::uniform_real_distribution<float> first(twitch_mode_ == 1 ? 20.0f : 2.0f, twitch_mode_ == 1 ? 45.0f : 5.0f);
            next_twitch_ = time_ + first(rng_);
        } else if (n == "flicker_on" || n == "flicker_off") {
            flicker_ = n == "flicker_on";
            if (flicker_ && !flicker_picture_.texture) flicker_picture_ = LoadPicture("bg_cafe_glitch");
            next_flicker_ = time_ + 1.5f;
        } else if (n == "photo") {
            flash_time_ = 0.5f;
            flash_color_ = glm::vec3(1.0f);
            photo_ = true;
            block_time_ = 0.7f;
            LogInfo("lisa date: photo");
        } else if (n == "darken") {
            light_target_.darken = 0.92f;
            scare_mood_ = true;
            light_target_.tint = glm::vec3(0.6f, 0.35f, 0.35f);
            block_time_ = 1.2f;
        } else if (n == "scare") {
            ApplyDirective({Op::Cam, "rush"});
            look_target_ = {14.0f, -6.0f, 0.0f, 0.0f, -0.3f, 1.0f, -1.0f, 1.0f, {1.2f, 0.4f, 0.4f}};
            Twitch(1.2f, 1.6f);
            shake_time_ = 1.6f;
            flash_time_ = 0.45f;
            flash_color_ = glm::vec3(0.9f, 0.05f, 0.05f);
            light_target_ = {{1.0f, 0.1f, 0.08f}, {0.3f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.6f, 0.1f, 0.1f}, 0.5f};
            light_ = light_target_;
            PlayScare();
            scare_time_ = 0.0f;
            block_time_ = 2.6f;
            LogInfo("lisa date: scare");
        } else {
            LogWarn("lisa date: unknown effect {}", n);
        }
        break;
    default:
        break;
    }
}

void LisaDate::SetLine(const std::string& speaker, const std::string& key) {
    speaker_ = speaker;
    line_key_ = key;
    line_ = text_.Get(key);
    line_chars_ = CountChars(line_);
    typed_ = 0.0f;
    shown_time_ = 0.0f;
    wait_ = Wait::Line;
    if (!skip_until_.empty() && key == skip_until_) {
        skip_until_.clear();
        skip_ = false;
    }
    LogInfo("lisa date: line {} ({})", key, speaker);
}

void LisaDate::FinishLine() {
    typed_ = static_cast<float>(line_chars_);
}

void LisaDate::Accept() {
    wait_ = Wait::None;
    AdvanceScript();
}

bool LisaDate::ChoicesUp() const {
    return phase_ == Phase::Scene && wait_ == Wait::Choice && run_ && run_->CurrentChoice();
}

bool LisaDate::Choose(int index) {
    if (!ChoicesUp()) return false;
    const LisaDateChoice* choice = run_->CurrentChoice();
    if (index < 0 || index >= static_cast<int>(choice->options.size())) return false;
    const LisaDateOption& o = choice->options[static_cast<size_t>(index)];
    const std::string key = o.key;
    if (!run_->Choose(index)) return false;
    LogInfo("lisa date: choice {} -> {} (affection {:+d}, now {}{})", choice->key, key, o.affection, run_->Affection(),
            o.flag.empty() ? "" : ", flag " + o.flag);
    wait_ = Wait::None;
    choice_buttons_.clear();
    skip_ = false;
    AdvanceScript();
    return true;
}

void LisaDate::SetSkip(bool on) {
    static const bool trace = std::getenv("PT_LISA_DATE_TRACE") != nullptr;
    if (trace && on != skip_) LogInfo("lisa date trace: skip {} at {}", on, line_key_);
    skip_ = on;
    if (on) auto_ = false;
}

void LisaDate::SkipTo(const std::string& key) {
    skip_until_ = key;
    SetSkip(true);
}

void LisaDate::Leave() {
    if (phase_ == Phase::Outro || phase_ == Phase::Done) return;
    LogInfo("lisa date: left from the menu (no ending)");
    StartOutro();
}

void LisaDate::RestartMusic() {
    if (!audio_ || phase_ == Phase::Outro || phase_ == Phase::Done) return;
    for (uint32_t* id : {&music_id_, &bed_id_, &scare_bed_id_}) {
        if (*id) audio_->StopPlayingId(*id, 0.0f);
        *id = 0;
    }
    music_name_.clear();
    UpdateMusic();
}

void LisaDate::StartOutro() {
    if (phase_ == Phase::Outro || phase_ == Phase::Done) return;
    phase_ = Phase::Outro;
    phase_time_ = 0.0f;
    wait_ = Wait::None;
    StopMusic(kOutroBlack);
    if (audio_) {
        for (uint32_t* id : {&bed_id_, &scare_bed_id_, &herald_id_}) {
            if (*id) audio_->StopPlayingId(*id, kOutroBlack);
            *id = 0;
        }
    }
}

std::string LisaDate::Glitched(const std::string& text, float amount) {
    if (amount <= 0.0f) return text;
    static constexpr char kNoise[] = "#%&@$*!?/\\|<>=+~^0123456789";
    std::mt19937 r(static_cast<uint32_t>(time_ * 10.0f) * 7919u + 17u);
    std::uniform_real_distribution<float> d(0.0f, 1.0f);
    std::string out;
    for (size_t i = 0; i < text.size(); i += CharLength(text, i)) {
        const size_t n = CharLength(text, i);
        if (text[i] != ' ' && d(r) < 0.16f * amount) out.push_back(kNoise[r() % (sizeof(kNoise) - 1)]);
        else out.append(text, i, n);
    }
    return out;
}

void LisaDate::DrawGradient(ui::UiBatch& batch, glm::vec2 lo, glm::vec2 hi, glm::vec4 top, glm::vec4 bottom, float alpha) {
    top.a *= alpha;
    bottom.a *= alpha;
    const glm::vec4 middle = glm::mix(glm::vec4(glm::vec3(kLavender), top.a), glm::vec4(glm::vec3(kLavender), bottom.a), 0.5f);
    const float my = (lo.y + hi.y) * 0.5f;
    const ui::UiVertex v[] = {
        {{lo.x, lo.y}, {0, 0}, top},    {{hi.x, lo.y}, {1, 0}, top},    {{hi.x, my}, {1, 0.5f}, middle},
        {{lo.x, lo.y}, {0, 0}, top},    {{hi.x, my}, {1, 0.5f}, middle}, {{lo.x, my}, {0, 0.5f}, middle},
        {{lo.x, my}, {0, 0.5f}, middle}, {{hi.x, my}, {1, 0.5f}, middle}, {{hi.x, hi.y}, {1, 1}, bottom},
        {{lo.x, my}, {0, 0.5f}, middle}, {{hi.x, hi.y}, {1, 1}, bottom}, {{lo.x, hi.y}, {0, 1}, bottom},
    };
    batch.Draw(v, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
}

void LisaDate::DrawHeart(ui::UiBatch& batch, glm::vec2 c, float size, glm::vec4 color) {
    constexpr int kSegments = 48;
    std::vector<ui::UiVertex> v;
    v.reserve(kSegments * 3);
    const float k = size / 34.0f;
    auto at = [&](float t) {
        const float s = std::sin(t);
        const float x = 16.0f * s * s * s;
        const float y = 13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t);
        return c + glm::vec2(x, -y - 2.5f) * k;
    };
    const glm::vec2 middle = c + glm::vec2(0.0f, -1.5f) * k;
    for (int i = 0; i < kSegments; ++i) {
        const float t0 = 6.2831853f * static_cast<float>(i) / kSegments;
        const float t1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
        v.push_back({middle, {0.5f, 0.5f}, color});
        v.push_back({at(t0), {0, 0}, color});
        v.push_back({at(t1), {0, 0}, color});
    }
    batch.Draw(v, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
}

void LisaDate::DrawBox(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 lo, glm::vec2 hi, glm::vec4 fill, glm::vec4 border, float width) {
    const auto plain = ui::UiDrawParams::Plain(TextureManager::kWhite);
    const glm::vec2 a = canvas.ToTarget(lo), b = canvas.ToTarget(hi);
    const float w = width * canvas.scale;
    batch.Quad(a, b, {0, 0}, {1, 1}, fill, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    if (border.a <= 0.0f || w <= 0.0f) return;
    batch.Quad(a, {b.x, a.y + w}, {0, 0}, {1, 1}, border, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    batch.Quad({a.x, b.y - w}, b, {0, 0}, {1, 1}, border, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    batch.Quad({a.x, a.y + w}, {a.x + w, b.y - w}, {0, 0}, {1, 1}, border, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
    batch.Quad({b.x - w, a.y + w}, {b.x, b.y - w}, {0, 0}, {1, 1}, border, plain, ui::UiShade::Solid, ui::UiBlend::Alpha);
}

void LisaDate::DrawLabel(ui::UiBatch& batch, const UiCanvas& canvas, const std::string& text, glm::vec2 lo, glm::vec2 hi, float size, glm::vec4 color,
                         ui::TextAlign align, size_t visible) {
    if (text.empty()) return;
    if (!font_ && !font_tried_) {
        font_tried_ = true;
        font_ = assets_.FontCovering(UiFontType::PcSystem, std::clamp(settings_.language, 0, UiAssets::kLanguageCount - 1), text_.All());
    }
    UiFont* font = font_ ? font_ : assets_.Font(UiFontType::PcSystem, std::clamp(settings_.language, 0, UiAssets::kLanguageCount - 1));
    if (!font) return;
    ui::TextStyle style;
    style.font = &font->font;
    style.font_width = style.font_height = size;
    style.line_space = size * 0.25f;
    ui::TextLayout layout = ui::LayoutText(text, style, hi.x - lo.x);
    if (visible == std::string::npos && hi.y - lo.y < 2.2f * size) {
        while (layout.lines.size() > 1 && style.font_height > 9.0f) {
            style.font_width = style.font_height = style.font_height * 0.9f;
            style.line_space = style.font_height * 0.25f;
            layout = ui::LayoutText(text, style, hi.x - lo.x);
        }
    }
    ui::PlaceText(layout, lo, hi, align, align, align == ui::TextAlign::Center ? ui::TextAlign::Center : ui::TextAlign::Start, true);
    if (visible != std::string::npos) {
        size_t left = visible;
        for (ui::TextLine& line : layout.lines) {
            if (line.glyphs.size() > left) {
                if (layout.rtl) line.glyphs.erase(line.glyphs.begin(), line.glyphs.end() - static_cast<std::ptrdiff_t>(left));
                else line.glyphs.resize(left);
            }
            left -= line.glyphs.size();
        }
    }
    DrawText(batch, canvas, *font, layout, color, ui::UiBlend::Alpha);
}

void LisaDate::DrawTransition(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full, const ui::UiDrawParams& noise) {
    const float t = phase_time_;
    (void)canvas;
    const float tearing = phase_ == Phase::Intro ? Smooth(t / 0.9f) * (1.0f - Smooth((t - kIntroCover) / 0.3f)) : 0.0f;
    if (tearing > 0.0f && noise.textures[0] && !settings_.replay) {
        std::mt19937 r(static_cast<uint32_t>(t * 24.0f) * 977u + 5u);
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        const int bands = 18;
        for (int i = 0; i < bands; ++i) {
            if (d(r) > 0.25f + 0.6f * tearing) continue;
            const float y0 = full.y * static_cast<float>(i) / bands;
            const float y1 = full.y * static_cast<float>(i + 1) / bands;
            const float shift = (d(r) - 0.5f) * 0.18f * tearing;
            ui::UiDrawParams p = noise;
            p.extra = glm::vec4(d(r), d(r), 0.6f * tearing, 1.0f);
            batch.Quad({0.0f, y0}, {full.x, y1}, {shift, y0 / full.y}, {1.0f + shift, y1 / full.y}, glm::vec4(1.0f, 0.82f + 0.1f * d(r), 0.92f, 1.0f), p,
                       ui::UiShade::Noise, ui::UiBlend::Alpha);
        }
    }
    if (phase_ == Phase::Intro && t > 0.45f && t < kIntroCover + 0.2f) {
        const float grow = Smooth((t - 0.45f) / 0.65f);
        DrawHeart(batch, full * 0.5f, full.y * (0.15f + 2.6f * grow * grow), glm::vec4(1.0f, 0.42f, 0.66f, 0.95f * (1.0f - Smooth((t - 1.05f) / 0.4f))));
        const float flash = std::max(0.0f, 1.0f - std::abs(t - 1.0f) / 0.25f);
        batch.Quad({0, 0}, full, {0, 0}, {1, 1}, glm::vec4(1.0f, 0.9f, 0.95f, 0.9f * flash), ui::UiDrawParams::Plain(TextureManager::kWhite),
                   ui::UiShade::Solid, ui::UiBlend::Alpha);
    }
    float cover = 0.0f;
    if (phase_ == Phase::Intro) cover = t < kIntroCover ? Smooth((t - 0.9f) / (kIntroCover - 0.9f)) : 1.0f - Smooth((t - 2.5f) / (kIntroLength - 2.5f));
    if (cover > 0.0f) DrawGradient(batch, {0, 0}, full, kPink, kPeach, cover);
    if (phase_ == Phase::Intro) {
        const float logo = Smooth((t - 1.15f) / 0.4f) * (1.0f - Smooth((t - 2.6f) / 0.5f));
        if (logo > 0.0f) {
            const float size = (0.62f + 0.04f * Smooth((t - 1.15f) / 2.0f)) * UiCanvas::kHeight;
            const glm::vec2 middle(0.5f * UiCanvas::kWidth, 0.48f * UiCanvas::kHeight);
            if (logo_.texture) {
                const glm::vec2 half(size * 0.5f * logo_.aspect, size * 0.5f);
                batch.Quad(canvas.ToTarget(middle - half), canvas.ToTarget(middle + half), {0, 0}, {1, 1}, glm::vec4(1.0f, 1.0f, 1.0f, logo),
                           ui::UiDrawParams::Plain(logo_.texture), ui::UiShade::Material, ui::UiBlend::Alpha);
            } else {
                DrawLabel(batch, canvas, "Coffee with Lisa", middle - glm::vec2(500.0f, 60.0f), middle + glm::vec2(500.0f, 60.0f), 64.0f,
                          glm::vec4(glm::vec3(kRose), logo), ui::TextAlign::Center);
            }
        }
    }
}

void LisaDate::DrawScene(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full) {
    const int language = std::clamp(settings_.language, 0, UiAssets::kLanguageCount - 1);
    (void)language;
    if (table_.texture && selfie_ == Selfie::Off) {
        const float dark = 1.0f - 0.85f * light_.darken;
        const glm::vec4 tint(light_.tint * dark, 1.0f);
        const ui::UiDrawParams table = ui::UiDrawParams::Plain(table_.texture);
        const float width = UiCanvas::kWidth * canvas.scale;
        const float height = width / table_.aspect;
        const float top = canvas.origin.y + (UiCanvas::kHeight * canvas.scale - height) + kTableShift * UiCanvas::kHeight * canvas.scale;
        const glm::vec2 lo(canvas.origin.x, top), hi(canvas.origin.x + width, top + height);
        batch.Quad(lo, hi, {0, 0}, {1, 1}, tint, table, ui::UiShade::Material, ui::UiBlend::Alpha);
        if (lo.x > 0.5f) {
            const float u = std::min(1.0f, lo.x / width);
            batch.Quad({0.0f, lo.y}, {lo.x, hi.y}, {u, 0}, {0, 1}, tint, table, ui::UiShade::Material, ui::UiBlend::Alpha);
            batch.Quad({hi.x, lo.y}, {full.x, hi.y}, {1, 0}, {1.0f - u, 1}, tint, table, ui::UiShade::Material, ui::UiBlend::Alpha);
        }
    }
    if (photo_ && wait_ != Wait::Ending) {
        const std::string caption = text_.Get("name_lisa") + " + " + text_.Get("name_you") + "  <3";
        if (selfie_ != Selfie::Off) DrawSelfieOutside(batch, canvas, full);
        if (polaroid_.texture) {
            const float size = 0.8f * UiCanvas::kHeight;
            const glm::vec2 hole(0.5f * UiCanvas::kWidth, 0.38f * UiCanvas::kHeight);
            const glm::vec2 middle = hole - glm::vec2(3.5f / 1254.0f, -41.0f / 1254.0f) * size;
            const glm::vec2 lo = middle - glm::vec2(size * 0.5f), hi = middle + glm::vec2(size * 0.5f);
            batch.Quad(canvas.ToTarget(lo), canvas.ToTarget(hi), {0, 0}, {1, 1}, glm::vec4(1.0f), ui::UiDrawParams::Plain(polaroid_.texture),
                       ui::UiShade::Material, ui::UiBlend::Alpha);
            const float strip = middle.y + (1067.0f - 627.0f) / 1254.0f * size;
            DrawLabel(batch, canvas, caption, {middle.x - 250.0f, strip - 26.0f}, {middle.x + 250.0f, strip + 26.0f}, 30.0f, kInk,
                      ui::TextAlign::Center);
        } else {
            const glm::vec2 lo(330.0f, 40.0f), hi(950.0f, 470.0f);
            const glm::vec4 paper(0.98f, 0.97f, 0.95f, 1.0f);
            DrawBox(batch, canvas, {lo.x - 18.0f, lo.y - 18.0f}, {hi.x + 18.0f, lo.y}, paper, glm::vec4(0.0f), 0.0f);
            DrawBox(batch, canvas, {lo.x - 18.0f, hi.y}, {hi.x + 18.0f, hi.y + 58.0f}, paper, glm::vec4(0.0f), 0.0f);
            DrawBox(batch, canvas, {lo.x - 18.0f, lo.y}, {lo.x, hi.y}, paper, glm::vec4(0.0f), 0.0f);
            DrawBox(batch, canvas, {hi.x, lo.y}, {hi.x + 18.0f, hi.y}, paper, glm::vec4(0.0f), 0.0f);
            DrawLabel(batch, canvas, caption, {lo.x, hi.y + 8.0f}, {hi.x, hi.y + 52.0f}, 26.0f, kInk, ui::TextAlign::Center);
        }
    }
    if (selfie_ == Selfie::Panel) DrawSelfiePanel(batch, canvas, full);
    const float g = glitch_ > 0.0f ? glitch_ : 0.0f;
    std::mt19937 jitter(static_cast<uint32_t>(time_ * 20.0f));
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    const glm::vec2 shake = g > 0.0f ? glm::vec2(4.0f * d(jitter), 2.0f * d(jitter)) : glm::vec2(0.0f);

    if (wait_ != Wait::Title && wait_ != Wait::Ending && selfie_ != Selfie::Saving) {
        const int filled = std::clamp((run_->Affection() + 2) / 4, 0, 5);
        DrawLabel(batch, canvas, text_.Get("ui_affection"), {880.0f, 22.0f}, {1060.0f, 50.0f}, 18.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.85f), ui::TextAlign::End);
        for (int i = 0; i < 5; ++i) {
            const glm::vec2 c = canvas.ToTarget({1082.0f + 34.0f * static_cast<float>(i), 37.0f});
            DrawHeart(batch, c, 30.0f * canvas.scale, glm::vec4(1.0f, 1.0f, 1.0f, 0.7f));
            DrawHeart(batch, c, 24.0f * canvas.scale, i < filled ? glm::vec4(1.0f, 0.38f, 0.6f, 1.0f) : glm::vec4(0.55f, 0.45f, 0.5f, 0.6f));
        }
    }

    if (wait_ == Wait::Line || wait_ == Wait::Choice || (wait_ == Wait::Block && !line_.empty())) {
        const glm::vec2 lo = glm::vec2(140.0f, 528.0f) + shake, hi = glm::vec2(1140.0f, 694.0f) + shake;
        DrawBox(batch, canvas, lo, hi, glm::vec4(1.0f, 0.97f, 0.98f, 0.93f), glm::vec4(0.96f, 0.6f, 0.74f, 1.0f), 4.0f);
        std::string name;
        if (speaker_ == "lisa") name = text_.Get("name_lisa");
        else if (speaker_ == "you") name = text_.Get("name_you");
        else if (speaker_ == "glitch") name = Glitched(text_.Get("name_glitch"), 3.0f);
        if (!name.empty()) {
            if (g > 0.0f) name = Glitched(name, 1.5f);
            DrawBox(batch, canvas, lo + glm::vec2(26.0f, -30.0f), lo + glm::vec2(214.0f, 6.0f), glm::vec4(1.0f, 0.7f, 0.82f, 1.0f),
                    glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 3.0f);
            DrawLabel(batch, canvas, name, lo + glm::vec2(30.0f, -28.0f), lo + glm::vec2(210.0f, 4.0f), 24.0f, glm::vec4(1.0f), ui::TextAlign::Center);
        }
        const bool glitch_line = g > 0.0f || speaker_ == "glitch";
        const std::string shown = glitch_line ? Glitched(line_, speaker_ == "glitch" ? 1.0f + g : g) : line_;
        glm::vec4 ink = kInk;
        if (glitch_line && d(jitter) > 0.4f) ink = glm::vec4(0.75f, 0.05f, 0.15f, 1.0f);
        if (speaker_ == "none" || speaker_ == "you") ink = glm::vec4(0.36f, 0.26f, 0.42f, 1.0f);
        DrawLabel(batch, canvas, shown, lo + glm::vec2(34.0f, 26.0f), hi - glm::vec2(34.0f, 30.0f), 25.0f, ink, ui::TextAlign::Start,
                  static_cast<size_t>(typed_));
        if (wait_ == Wait::Line && typed_ >= static_cast<float>(line_chars_) && std::fmod(time_, 1.0f) < 0.6f) {
            const glm::vec2 c = canvas.ToTarget(hi - glm::vec2(30.0f, 22.0f));
            DrawHeart(batch, c, 16.0f * canvas.scale, kRose);
        }
        if (wait_ == Wait::Line) DrawPrompt(batch, canvas, PromptButton::Cross, KeyAction::Confirm, {hi.x - 46.0f, hi.y - 22.0f}, 21.0f, 1.0f, true);
        {
            float x = DrawHint(batch, canvas, PromptButton::Options, KeyAction::Menu, text_.Get("ui_hint_menu"), {lo.x + 34.0f, hi.y - 18.0f}, 0.85f);
            if (prompts_.device != PromptDevice::Keyboard) {
                DrawHint(batch, canvas, PromptButton::Circle, KeyAction::Cancel, text_.Get("ui_leave_hold"), {x + 22.0f, hi.y - 18.0f}, 0.85f);
            }
        }
        auto_button_ = {{hi.x - 254.0f, lo.y - 26.0f}, {hi.x - 144.0f, lo.y + 2.0f}};
        skip_button_ = {{hi.x - 134.0f, lo.y - 26.0f}, {hi.x - 24.0f, lo.y + 2.0f}};
        for (int i = 0; i < 2; ++i) {
            const Button& b = i == 0 ? auto_button_ : skip_button_;
            const bool on = i == 0 ? auto_ : skip_;
            const bool hover = pointer_valid_ && b.Contains(pointer_);
            DrawBox(batch, canvas, b.lo, b.hi, on ? glm::vec4(0.96f, 0.55f, 0.72f, 0.95f) : glm::vec4(1.0f, 1.0f, 1.0f, hover ? 0.95f : 0.75f),
                    glm::vec4(0.96f, 0.6f, 0.74f, 1.0f), 2.0f);
            const float glyph = DrawPrompt(batch, canvas, i == 0 ? PromptButton::Triangle : PromptButton::Square, i == 0 ? KeyAction::DateAuto : KeyAction::DateSkip,
                                           {b.lo.x + 5.0f, 0.5f * (b.lo.y + b.hi.y)}, 19.0f, 1.0f);
            DrawLabel(batch, canvas, text_.Get(i == 0 ? "ui_auto" : "ui_skip"), {b.lo.x + glyph + 7.0f, b.lo.y}, b.hi, 17.0f, on ? glm::vec4(1.0f) : kInk,
                      ui::TextAlign::Center);
        }
    }

    if (wait_ == Wait::Choice) {
        const LisaDateChoice* choice = run_->CurrentChoice();
        const size_t count = choice ? choice->options.size() : 0;
        choice_buttons_.resize(count);
        const float height = 56.0f, gap = 16.0f;
        const float top = 270.0f - (static_cast<float>(count) * (height + gap) - gap) * 0.5f;
        for (size_t i = 0; i < count; ++i) {
            Button& b = choice_buttons_[i];
            b.lo = {340.0f, top + static_cast<float>(i) * (height + gap)};
            b.hi = {940.0f, b.lo.y + height};
            const bool on = static_cast<int>(i) == selected_;
            DrawBox(batch, canvas, b.lo, b.hi, on ? glm::vec4(1.0f, 0.74f, 0.85f, 0.97f) : glm::vec4(1.0f, 0.98f, 0.99f, 0.93f),
                    glm::vec4(0.96f, 0.6f, 0.74f, 1.0f), on ? 4.0f : 2.0f);
            std::string label = text_.Get(choice->options[i].key);
            if (g > 0.0f) label = Glitched(label, g);
            DrawLabel(batch, canvas, label, b.lo + glm::vec2(16.0f, 4.0f), b.hi - glm::vec2(on ? 52.0f : 16.0f, 4.0f), 23.0f, kInk, ui::TextAlign::Center);
            if (on) DrawPrompt(batch, canvas, PromptButton::Cross, KeyAction::Confirm, {b.hi.x - 14.0f, 0.5f * (b.lo.y + b.hi.y)}, 22.0f, 1.0f, true);
        }
        if (count > 1) {
            DrawPrompt(batch, canvas, PromptButton::DpadUpDown, KeyAction::PadUp, {326.0f, top + 0.5f * (static_cast<float>(count) * (height + gap) - gap)}, 30.0f,
                       1.0f, true, KeyAction::PadDown);
        }
    }

    if (wait_ == Wait::Title) {
        const float a = std::min(1.0f, (2.4f - wait_time_) / 0.3f) * std::min(1.0f, wait_time_ / 0.35f);
        DrawGradient(batch, {0, canvas.ToTarget({0, 290.0f}).y}, {full.x, canvas.ToTarget({0, 430.0f}).y}, kPink, kPeach, 0.92f * a);
        DrawLabel(batch, canvas, title_, {100.0f, 300.0f}, {1180.0f, 420.0f}, 44.0f, glm::vec4(glm::vec3(kInk), a), ui::TextAlign::Center);
    }
    if (wait_ == Wait::Ending) {
        const float a = std::min(1.0f, (4.5f - wait_time_) / 0.6f);
        const bool bad = ending_ == "bad";
        if (bad) {
            batch.Quad({0, 0}, full, {0, 0}, {1, 1}, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f * a), ui::UiDrawParams::Plain(TextureManager::kWhite),
                       ui::UiShade::Solid, ui::UiBlend::Alpha);
        } else {
            DrawGradient(batch, {0, 0}, full, kPink, ending_ == "true" ? kLavender : kPeach, 0.9f * a);
        }
        std::string title = ending_title_;
        if (ending_ == "true") title = Glitched(title, 0.5f);
        DrawLabel(batch, canvas, title, {80.0f, 300.0f}, {1200.0f, 420.0f}, 46.0f,
                  bad ? glm::vec4(0.85f, 0.1f, 0.12f, a) : glm::vec4(glm::vec3(kInk), a), ui::TextAlign::Center);
    }
    auto note = [&](const std::string& text, float alpha, glm::vec2 lo, glm::vec2 hi, float size) {
        if (alpha <= 0.0f || text.empty()) return;
        DrawBox(batch, canvas, lo, hi, glm::vec4(1.0f, 0.97f, 0.98f, 0.9f * alpha), glm::vec4(glm::vec3(0.96f, 0.6f, 0.74f), alpha), 2.0f);
        DrawLabel(batch, canvas, text, lo + glm::vec2(12.0f, 6.0f), hi - glm::vec2(12.0f, 6.0f), size, glm::vec4(glm::vec3(kInk), alpha), ui::TextAlign::Start);
    };
    note(photo_note_, std::min(1.0f, photo_note_time_ / 0.5f), {24.0f, 16.0f}, {760.0f, 54.0f}, 17.0f);
    if (leave_hold_ > 0.15f) {
        const float share = std::clamp(leave_hold_ / 2.0f, 0.0f, 1.0f);
        DrawBox(batch, canvas, {40.0f, 60.0f}, {340.0f, 104.0f}, glm::vec4(1.0f, 0.97f, 0.98f, 0.9f), glm::vec4(0.96f, 0.6f, 0.74f, 1.0f), 2.0f);
        DrawBox(batch, canvas, {44.0f, 96.0f}, {44.0f + 292.0f * share, 100.0f}, kRose, glm::vec4(0.0f), 0.0f);
        const float glyph = DrawPrompt(batch, canvas, PromptButton::Circle, KeyAction::Cancel, {50.0f, 79.0f}, 20.0f, 1.0f);
        DrawLabel(batch, canvas, text_.Get("ui_leave_hold"), {56.0f + glyph, 62.0f}, {330.0f, 94.0f}, 18.0f, kInk, ui::TextAlign::Center);
    }
    if (g > 0.0f || tear_ > 0.0f) {
        std::mt19937 r(static_cast<uint32_t>(time_ * 15.0f) * 31u);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int i = 0; i < 5; ++i) {
            if (u(r) > 0.45f * std::max(g, tear_)) continue;
            const float y = u(r) * full.y;
            const float h = (2.0f + 10.0f * u(r)) * canvas.scale;
            const glm::vec4 c = u(r) > 0.5f ? glm::vec4(0.2f, 1.0f, 0.9f, 0.35f) : glm::vec4(1.0f, 0.2f, 0.6f, 0.35f);
            batch.Quad({0.0f, y}, {full.x, y + h}, {0, 0}, {1, 1}, c, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid,
                       ui::UiBlend::Additive);
        }
    }
    if (flash_time_ > 0.0f) {
        batch.Quad({0, 0}, full, {0, 0}, {1, 1}, glm::vec4(flash_color_, std::min(1.0f, flash_time_ / 0.3f)), ui::UiDrawParams::Plain(TextureManager::kWhite),
                   ui::UiShade::Solid, ui::UiBlend::Alpha);
    }
}

void LisaDate::DrawUi(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full, const ui::UiDrawParams& noise) {
    canvas_ = canvas;
    full_ = full;
    canvas_ready_ = true;
    static const bool trace = std::getenv("PT_LISA_DATE_TRACE") != nullptr;
    if (trace) LogInfo("lisa date trace: phase {} wait {} block {:.2f} line {} typed {:.0f}/{}", static_cast<int>(phase_), static_cast<int>(wait_), block_time_, line_key_, typed_, line_chars_);
    if (showing_scene_) DrawScene(batch, canvas, full);
    if (phase_ == Phase::Intro) DrawTransition(batch, canvas, full, noise);
    if (phase_ == Phase::Outro || phase_ == Phase::Done) {
        const float t = phase_time_;
        const float a = t < kOutroBlack ? Smooth(t / kOutroBlack) : 1.0f - Smooth((t - kOutroBlack) / (kOutroLength - kOutroBlack));
        batch.Quad({0, 0}, full, {0, 0}, {1, 1}, glm::vec4(0.0f, 0.0f, 0.0f, phase_ == Phase::Done ? 0.0f : a),
                   ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
    }
}

}
