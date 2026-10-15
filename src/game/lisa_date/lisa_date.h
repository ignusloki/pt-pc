#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "engine/platform/input.h"
#include "engine/render/camera.h"
#include "engine/render/mesh.h"
#include "engine/render/scene_renderer.h"
#include "engine/ui/ui_batch.h"
#include "game/lisa_date/lisa_date_audio.h"
#include "game/lisa_date/lisa_date_script.h"
#include "game/ui/ui_icons.h"
#include "game/ui/uif_view.h"

namespace pt::anim {
class HelpBones;
}

namespace pt {
class ModelCache;
class Vfs;
struct SceneLighting;
}

namespace pt::game {

class GameAudio;
class UiAssets;
struct UiFont;

class LisaDate {
public:
    struct Settings {
        int language = 0;
        bool replay = false;
        std::string cause;
        const GpuMesh* lisa_mesh = nullptr;
        MeshMask lisa_hidden;
        const GpuMesh* player_mesh = nullptr;
        MeshMask player_hidden;
        std::shared_ptr<const anim::HelpBones> player_help;
    };

    LisaDate(Vfs& vfs, ModelCache& models, UiAssets& assets, GameAudio* audio);
    ~LisaDate();

    bool Start(const Settings& settings);
    void Update(float dt, const InputState& input);
    bool ShowingScene() const { return showing_scene_; }
    bool Finished() const { return phase_ == Phase::Done; }
    void CollectDraws(std::vector<DrawItem>& out);
    Camera ViewCamera() const;
    void ApplyLighting(SceneLighting& lighting) const;
    SceneRenderer::Backdrop Backdrop() const;
    void DrawUi(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full, const ui::UiDrawParams& noise);

    void Leave();
    void RestartMusic();
    bool ChoicesUp() const;
    bool Choose(int index);
    void SetSkip(bool on);
    void SetExpression(const std::string& name) { ApplyDirective({LisaDateCommand::Op::Expr, name}); }
    void TwitchNow(bool violent) { violent ? Twitch(1.1f, 2.0f) : Twitch(0.4f, 0.6f); }
    void SkipTo(const std::string& key);
    const std::string& LineKey() const { return line_key_; }
    std::string_view Ending() const { return ending_; }
    bool Completed() const { return !ending_.empty(); }
    int Affection() const { return run_ ? run_->Affection() : 0; }

    std::optional<glm::vec4> TakePhotoRequest() { return std::exchange(photo_request_, std::nullopt); }
    bool SceneStill() const { return frozen_; }
    void PhotoSaved(const std::string& file, bool ok);
    bool SelfieUp() const { return phase_ == Phase::Scene && wait_ == Wait::Selfie && selfie_ == Selfie::Panel; }
    void SetSelfie(int lisa_face, int lisa_tilt, int you_face, int you_tilt, bool take);

    static constexpr int kUiPriority = 169;

private:
    enum class Phase { Intro, Scene, Outro, Done };
    enum class Wait { None, Line, Choice, Timer, Title, Ending, Block, Selfie };

    struct Look {
        float lean = 0.0f;
        float pitch = 0.0f;
        float roll = 0.0f;
        float yaw = 0.0f;
        float smile = 0.0f;
        float jaw = 0.0f;
        float lids = 0.0f;
        float zoom = 1.0f;
        glm::vec3 tint{1.0f};
    };
    struct LightSet {
        glm::vec3 key{1.0f};
        glm::vec3 fill{1.0f};
        glm::vec3 rim{1.0f};
        glm::vec3 tint{1.0f};
        float darken = 0.0f;
    };
    struct Shot {
        glm::vec3 target{0.0f};
        float fov = 30.0f;
    };
    struct Button {
        glm::vec2 lo{0.0f};
        glm::vec2 hi{0.0f};
        bool Contains(glm::vec2 p) const { return p.x >= lo.x && p.y >= lo.y && p.x <= hi.x && p.y <= hi.y; }
    };

    void LoadText();
    void LoadMusic();
    void PlayMusic(const std::string& name);
    void StopMusic(float fade);
    void UpdateMusic();
    void PlayScare();
    void BuildPose();
    void BuildFallbackPose();
    void PoseFrame();
    void AdvanceScript();
    void HandleStop(int stop);
    void ApplyDirective(const LisaDateRun::Directive& d);
    void SetLine(const std::string& speaker, const std::string& key);
    void FinishLine();
    void Accept();
    void StartOutro();
    std::string Glitched(const std::string& text, float amount);

    enum class Selfie { Off, Panel, Developing, Saving, Shown };
    void StartSelfie();
    void UpdateSelfie(float dt, const InputState& input, bool up, bool down, bool left, bool right, bool accept, bool clicked, bool pointer_moved);
    void TakeSelfie();
    void PosePlayer();
    Look SelfieLook() const;
    void DrawSelfiePanel(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full);
    void DrawSelfieOutside(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full);
    void DrawArrow(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 center, float size, bool right, glm::vec4 color);
    float DrawPrompt(ui::UiBatch& batch, const UiCanvas& canvas, PromptButton button, KeyAction key, glm::vec2 at, float height, float alpha,
                     bool from_right = false, KeyAction key2 = KeyAction::Confirm);
    float DrawHint(ui::UiBatch& batch, const UiCanvas& canvas, PromptButton button, KeyAction key, const std::string& words, glm::vec2 at, float alpha,
                   KeyAction key2 = KeyAction::Confirm);

    void DrawTransition(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full, const ui::UiDrawParams& noise);
    void DrawScene(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 full);
    void DrawHeart(ui::UiBatch& batch, glm::vec2 center, float size, glm::vec4 color);
    void DrawGradient(ui::UiBatch& batch, glm::vec2 lo, glm::vec2 hi, glm::vec4 top, glm::vec4 bottom, float alpha);
    void DrawBox(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 lo, glm::vec2 hi, glm::vec4 fill, glm::vec4 border, float width);
    void DrawLabel(ui::UiBatch& batch, const UiCanvas& canvas, const std::string& text, glm::vec2 lo, glm::vec2 hi, float size, glm::vec4 color,
                   ui::TextAlign align, size_t visible = std::string::npos);

    Vfs& vfs_;
    ModelCache& models_;
    UiAssets& assets_;
    GameAudio* audio_;
    Settings settings_;
    std::filesystem::path dir_;

    LisaDateScript script_;
    LisaDateText text_;
    UiFont* font_ = nullptr;
    bool font_tried_ = false;
    std::unique_ptr<LisaDateRun> run_;

    Phase phase_ = Phase::Intro;
    Wait wait_ = Wait::None;
    float phase_time_ = 0.0f;
    float time_ = 0.0f;
    float wait_time_ = 0.0f;
    float block_time_ = 0.0f;
    int pending_stop_ = -1;
    bool showing_scene_ = false;

    std::string speaker_;
    std::string line_key_;
    std::string line_;
    float typed_ = 0.0f;
    size_t line_chars_ = 0;
    bool auto_ = false;
    bool skip_ = false;
    std::string skip_until_;
    float shown_time_ = 0.0f;
    std::string title_;
    std::string ending_;
    std::string ending_title_;
    int selected_ = 0;
    bool stick_held_ = false;
    bool stick_held_x_ = false;
    float leave_hold_ = 0.0f;

    Look look_;
    Look look_target_;
    std::string expression_ = "neutral";
    LightSet light_;
    LightSet light_target_;
    std::string light_name_ = "warm";
    Shot shot_;
    Shot shot_target_;
    float shot_rate_ = 2.5f;
    float twitch_time_ = 0.0f;
    float twitch_length_ = 0.9f;
    float twitch_amp_ = 1.0f;
    int twitch_mode_ = 0;
    float next_twitch_ = 0.0f;
    float blink_time_ = 0.0f;
    float next_blink_ = 2.0f;
    bool flicker_ = false;
    float next_flicker_ = 0.0f;
    float flicker_until_ = -1.0f;
    void Twitch(float length, float amp);
    float shake_time_ = 0.0f;
    float flash_time_ = 0.0f;
    glm::vec3 flash_color_{1.0f};
    float glitch_ = 0.0f;
    float tear_ = 0.0f;
    bool photo_ = false;

    Selfie selfie_ = Selfie::Off;
    int selfie_values_[4] = {0, 0, 0, 0};
    float selfie_shown_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int selfie_row_ = 0;
    float selfie_time_ = 0.0f;
    float saving_time_ = 0.0f;
    bool frozen_ = false;
    float frozen_time_ = 0.0f;
    Look photo_look_;
    std::optional<glm::vec4> photo_request_;
    std::string photo_note_;
    float photo_note_time_ = 0.0f;
    struct SelfieButtons {
        Button rows[5];
        Button minus[4];
        Button plus[4];
    } selfie_buttons_;
    bool selfie_buttons_ready_ = false;
    bool selfie_camera_ = false;
    glm::vec3 selfie_eye_{0.0f};
    glm::vec3 selfie_target_{0.0f};
    float selfie_fov_ = 60.0f;
    const GpuMesh* player_mesh_ = nullptr;
    MeshMask player_hidden_;
    std::shared_ptr<const anim::HelpBones> player_help_;
    std::vector<glm::mat4> player_skin_;
    glm::mat4 player_transform_{1.0f};
    float player_idle_ = 0.0f;
    std::vector<uint8_t> shutter_wav_;
    float scare_time_ = -1.0f;
    std::mt19937 rng_{0x11A5Au};

    struct Picture {
        uint32_t texture = 0;
        float aspect = 16.0f / 9.0f;
    };
    Picture LoadPicture(const std::string& name);
    std::string backdrop_name_ = "cafe";
    Picture backdrop_;
    Picture table_;
    Picture logo_;
    Picture polaroid_;
    Picture flicker_picture_;

    std::shared_ptr<const LisaDatePcm> music_;
    LisaMusicLoop music_loop_;
    std::shared_ptr<const LisaDatePcm> music_slow_;
    LisaMusicLoop music_slow_loop_;
    std::string music_name_;
    uint32_t music_id_ = 0;
    std::shared_ptr<LisaMusicState> music_state_;
    LisaMusicLoop music_playing_loop_;
    uint64_t music_playing_frames_ = 0;
    uint32_t music_playing_rate_ = 0;
    uint32_t music_seams_logged_ = 0;
    bool music_outro_logged_ = false;
    bool music_end_logged_ = false;
    bool music_ending_ = false;
    void ReleaseMusic();
    bool MusicPlayingOut() const;
    std::string music_wanted_;
    int mood_ = 0;
    float mood_since_ = 0.0f;
    bool scare_mood_ = false;
    uint32_t bed_id_ = 0;
    uint32_t scare_bed_id_ = 0;
    uint32_t herald_id_ = 0;
    bool bed_failed_ = false;
    bool scare_bed_failed_ = false;
    int Mood() const;
    void SetMood(int mood);
    uint32_t scare_id_ = 0;

    const GpuMesh* lisa_mesh_ = nullptr;
    MeshMask lisa_hidden_;
    std::vector<int> parents_;
    std::vector<glm::vec3> bind_;
    std::vector<std::string> bone_names_;
    std::vector<glm::mat4> seated_;
    std::vector<glm::mat4> skin_;
    std::string pose_source_;

    UiCanvas canvas_;
    glm::vec2 full_{0.0f};
    bool canvas_ready_ = false;
    std::vector<Button> choice_buttons_;
    Button auto_button_;
    Button skip_button_;
    PromptStyle prompts_;
    glm::vec2 pointer_{-1.0f};
    glm::vec2 last_pointer_{-1.0f};
    bool pointer_valid_ = false;
};

}
