#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/platform/gyro.h"

union SDL_Event;
struct SDL_Gamepad;

namespace pt {

enum PadRaw : uint32_t {
    kRawSquare = 1u << 0,
    kRawCross = 1u << 1,
    kRawCircle = 1u << 2,
    kRawTriangle = 1u << 3,
    kRawL2 = 1u << 4,
    kRawR2 = 1u << 5,
    kRawL1 = 1u << 6,
    kRawR1 = 1u << 7,
    kRawStart = 1u << 8,
    kRawSelect = 1u << 9,
    kRawL3 = 1u << 10,
    kRawR3 = 1u << 11,
    kRawUp = 1u << 12,
    kRawDown = 1u << 13,
    kRawLeft = 1u << 14,
    kRawRight = 1u << 15,
};

enum PadButton : uint32_t {
    kPadZoom = 1u << 0,
    kPadL1 = 1u << 1,
    kPadR1 = 1u << 2,
    kPadTriangle = 1u << 3,
    kPadL3 = 1u << 4,
    kPadAction = 1u << 5,
    kPadSelect = 1u << 6,
    kPadLookUp = 1u << 7,
    kPadLookDown = 1u << 8,
    kPadLookLeft = 1u << 9,
    kPadLookRight = 1u << 10,
    kPadGouge = 1u << 11,
};

uint32_t PlayerButtonsFromRaw(uint32_t raw);

enum class KeyAction : uint8_t {
    WalkForward, WalkBack, WalkLeft, WalkRight, PadUp, PadDown, PadLeft, PadRight, Zoom, Act, Menu, Confirm, Cancel, PcSettings, MenuColumn,
    MenuQuit, MenuHouse, FastWalk,
    DateAuto, DateSkip,
    PhotoSave, PhotoHide, FreeRise, FreeSink, FreeFast,
    MusicBack, MusicForward, MusicRestart, MusicLoop,
    MenuTabPrev, MenuTabNext, MenuDonate
};

inline constexpr uint32_t kMenuKeyTabPrev = 1;
inline constexpr uint32_t kMenuKeyTabNext = 2;
inline constexpr uint32_t kMenuKeyDonate = 4;
inline constexpr uint32_t kMenuKeyFromE = 8;

inline constexpr uint32_t kMusicPressBack = 1;
inline constexpr uint32_t kMusicPressForward = 2;
inline constexpr uint32_t kMusicPressRestart = 4;
inline constexpr uint32_t kMusicPressLoop = 8;

struct KeyBinding {
    KeyAction action;
    uint16_t scancode;
    uint8_t mouse;
};

std::span<const KeyBinding> KeyBindings();
const KeyBinding* FirstBinding(KeyAction action, bool keys_only = false);
std::string KeyBindingName(const KeyBinding& binding);
using KeyLayoutFunction = uint32_t (*)(uint16_t scancode);
void SetKeyLayoutForTest(KeyLayoutFunction layout);

enum class PromptDevice : uint8_t { Keyboard, PlayStation, Xbox, Nintendo, Steam };
const char* PromptDeviceName(PromptDevice device);

bool IsSteamGamepadId(uint16_t vendor, uint16_t product);

struct PromptStyle {
    PromptDevice device = PromptDevice::Keyboard;
    std::array<char, 4> faces{};

    bool operator==(const PromptStyle&) const = default;
};

struct InputState {
    glm::vec2 left_stick{0.0f};
    glm::vec2 right_stick{0.0f};
    glm::vec2 mouse_look{0.0f};
    uint32_t held = 0;
    uint32_t pressed = 0;
    uint32_t raw_held = 0;
    uint32_t raw_pressed = 0;
    bool pause = false;
    bool pause_held = false;
    bool confirm = false;
    bool cancel = false;
    bool pc_settings = false;
    bool any_button = false;
    bool from_gamepad = false;
    bool left_stick_from_pad = false;
    bool fast_walk = false;
    bool click = false;
    bool right_click = false;
    float wheel = 0.0f;
    bool gouge_pressed = false;
    bool voice_keyword_pressed = false;
    bool house_pressed = false;
    bool date_auto_pressed = false;
    bool date_skip_pressed = false;
    uint32_t music_pressed = 0;
    uint32_t menu_keys_pressed = 0;
    bool capture_pose_pressed = false;
    bool pointer_valid = false;
    glm::vec2 pointer{0.0f};
    PromptStyle prompts;
    bool vr_look = false;
    glm::vec2 vr_look_angles{0.0f};
    float gamepad_sensitivity = 1.0f;
    glm::vec2 gyro_turn{0.0f};
    glm::vec2 gyro_light{0.0f};
};

void MergeFrameButtons(InputState& pending, const InputState& polled);

struct InputSettings {
    float mouse_sensitivity = 0.0015f;
    float gamepad_sensitivity = 1.0f;
    float stick_dead_zone = 26.0f / 255.0f;
    bool rumble = true;
    bool trigger_rumble = false;
    int prompt_style = 0;
    int gyro = 0;
    float gyro_sensitivity = 1.0f;
    float gyro_tightening = 4.0f;
    std::map<std::string, std::array<float, 3>> gyro_bias;
};

struct GamepadInfo {
    uint32_t id = 0;
    bool is_gamepad = false;
    bool opened = false;
    bool is_virtual = false;
    std::string name;
    std::string type;
    std::string path;
    std::string connection;
    std::string serial;
    std::string power;
    std::string mapping;
    uint16_t vendor = 0;
    uint16_t product = 0;
    int player_index = -1;
    int touchpads = 0;
    bool rumble = false;
    bool trigger_rumble = false;
    bool gyro = false;
};

std::vector<GamepadInfo> DescribeJoysticks();
std::string DescribeGamepad(const GamepadInfo& info);
void LogJoysticks(uint32_t wait_ms);
uint32_t ScancodeFromName(std::string_view name);
uint32_t MouseButtonFromName(std::string_view name);

enum class MouseUse { None, Look, Menu };

class KeyPressLatch {
public:
    void ProcessEvent(const SDL_Event& event, uint32_t scancode);
    bool Consume() { return std::exchange(pressed_, false); }
    void Discard() { pressed_ = false; }

private:
    bool pressed_ = false;
};

class InputDevice {
public:
    void Init();
    void Shutdown();
    void ProcessEvent(const SDL_Event& event);
    InputState Poll(bool keyboard_free, MouseUse mouse, bool pads_free = true);
    void SetRumble(uint8_t large_motor, uint8_t small_motor);
    void SetTriggerRumble(uint8_t left, uint8_t right);
    size_t GamepadCount() const { return pads_.size(); }
    uint32_t RumblePad() const { return rumble_pad_; }
    SDL_Gamepad* LastUsedGamepad() const;
    PromptStyle Prompts() const { return ShownPrompts(); }
    void InjectKey(uint32_t scancode, bool down);
    void InjectMouseButton(uint8_t button, bool down);
    void InjectMouseMotion(float dx, float dy, int polls);
    const GyroTracker& Gyro() const { return gyro_; }
    void StartGyroCalibration() { gyro_.StartCalibration(); }
    void ClearGyroCalibration() { gyro_.ClearCalibration(); }
    GyroTracker::Calibration GyroCalibration() const { return gyro_.CalibrationState(); }
    float GyroCalibrationProgress() const { return gyro_.CalibrationProgress(); }
    bool AnyGyroPad() const;
    bool TakeGyroCalibrated(std::string& guid, std::array<float, 3>& offset);

    InputSettings settings;

private:
    struct Pad {
        SDL_Gamepad* handle = nullptr;
        uint32_t id = 0;
        uint32_t event_raw = 0;
        uint64_t last_used = 0;
        PromptStyle style;
        uint32_t previous_gouge = 0;
        bool previous_voice_chord = false;
        bool trigger_rumble_supported = false;
        bool has_gyro = false;
        bool gyro_on = false;
        std::string guid;
        uint64_t gyro_time = 0;
    };

    void Open(uint32_t id);
    void Close(uint32_t id);
    Pad* Find(uint32_t id);
    void UsePrompts(const PromptStyle& style, uint32_t pad);
    uint32_t HeldRaw(const Pad& pad) const;
    uint32_t TouchRaw(const Pad& pad) const;
    glm::vec2 Stick(const Pad& pad, int x_axis, int y_axis) const;
    void UpdateRumble();
    void UpdateGyroSensors();

    std::vector<Pad> pads_;
    GyroTracker gyro_;
    uint32_t gyro_pad_ = 0;
    bool gyro_missing_logged_ = false;
    std::string calibrated_guid_;
    std::array<float, 3> calibrated_offset_{};
    bool calibrated_ready_ = false;
    uint64_t use_counter_ = 0;
    float mouse_dx_ = 0.0f;
    float mouse_dy_ = 0.0f;
    uint32_t previous_held_ = 0;
    uint32_t previous_raw_ = 0;
    bool previous_gouge_key_ = false;
    bool previous_house_key_ = false;
    uint32_t previous_menu_keys_ = 0;
    bool pad_back_edge_ = false;
    bool previous_date_auto_key_ = false;
    bool previous_date_skip_key_ = false;
    bool capture_pose_edge_ = false;
    uint32_t previous_music_keys_ = 0;
    bool pause_edge_ = false;
    bool confirm_edge_ = false;
    bool cancel_edge_ = false;
    bool click_edge_ = false;
    bool right_click_edge_ = false;
    float wheel_ = 0.0f;
    bool settings_edge_ = false;
    bool any_edge_ = false;
    bool last_from_gamepad_ = false;
    std::vector<bool> injected_keys_;
    uint32_t injected_mouse_ = 0;
    glm::vec2 injected_motion_{0.0f};
    int injected_motion_polls_ = 0;
    uint32_t mouse_down_latch_ = 0;
    PromptStyle prompts_;
    PromptStyle ShownPrompts() const;
    uint32_t prompt_pad_ = 0;
    float mouse_travel_ = 0.0f;
    bool key_pressed_since_poll_ = false;
    MouseUse last_mouse_use_ = MouseUse::None;
    int mouse_settle_ = 0;
    uint32_t rumble_pad_ = 0;
    uint32_t rumble_logged_pad_ = 0;
    uint8_t rumble_want_[2] = {0, 0};
    uint8_t rumble_sent_[2] = {0, 0};
    uint8_t trigger_rumble_want_[2] = {0, 0};
    uint16_t trigger_rumble_sent_[2] = {0, 0};
    uint64_t trigger_rumble_sent_at_ = 0;
    uint64_t rumble_sent_at_ = 0;
};

}
