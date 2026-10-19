#include "engine/core/ascii.h"
#include "engine/platform/os.h"
#include "engine/platform/settings.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <cmath>
#include <map>
#include <sstream>

#include "engine/core/log.h"
#include "engine/platform/graphics_presets.h"

namespace pt {
namespace {

using Values = std::map<std::string, std::string>;

std::string Trim(const std::string& text) {
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

Values Parse(std::istream& in) {
    Values values;
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string value = Trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        values[section + "." + Trim(line.substr(0, eq))] = value;
    }
    return values;
}

void Read(const Values& values, const char* key, int& out) {
    if (auto it = values.find(key); it != values.end()) {
        try {
            out = std::stoi(it->second);
        } catch (...) {
            LogWarn("settings: {} = '{}' is not a number", key, it->second);
        }
    }
}

void Read(const Values& values, const char* key, float& out) {
    if (auto it = values.find(key); it != values.end()) {
        try {
            out = std::stof(it->second);
        } catch (...) {
            LogWarn("settings: {} = '{}' is not a number", key, it->second);
        }
    }
}

void Read(const Values& values, const char* key, bool& out) {
    if (auto it = values.find(key); it != values.end()) {
        std::string v = it->second;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(pt::AsciiLower(static_cast<char>(c))); });
        out = v == "1" || v == "true" || v == "yes" || v == "on";
    }
}

void Read(const Values& values, const char* key, std::string& out) {
    if (auto it = values.find(key); it != values.end()) {
        out = it->second;
    }
}

}

bool LoadAppSettings(const std::filesystem::path& path, AppSettings& out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    const Values v = Parse(in);
    Read(v, "display.width", out.display.width);
    Read(v, "display.height", out.display.height);
    Read(v, "display.fullscreen", out.display.fullscreen);
    Read(v, "display.vsync", out.display.vsync);
    Read(v, "display.pause_on_focus_loss", out.display.pause_on_focus_loss);
    Read(v, "display.mute_in_background", out.display.mute_in_background);
    Read(v, "display.fps_limit", out.display.fps_limit);
    Read(v, "display.hdr", out.display.hdr);
    Read(v, "display.hdr_paper_white", out.display.hdr_paper_white);
    Read(v, "display.hdr_peak", out.display.hdr_peak);
    Read(v, "display.letterbox", out.display.letterbox);
    Read(v, "display.subtitle_scale", out.display.subtitle_scale);
    Read(v, "display.rendering_api", out.display.rendering_api);
    Read(v, "display.rendering_api_asked", out.display.rendering_api_asked);
    Read(v, "display.d3d12_crashed", out.display.d3d12_crashed);
    Read(v, "input.mouse_sensitivity", out.input.mouse_sensitivity);
    Read(v, "input.gamepad_sensitivity", out.input.gamepad_sensitivity);
    Read(v, "input.gamepad_dead_zone", out.input.gamepad_dead_zone);
    Read(v, "input.rumble", out.input.rumble);
    Read(v, "input.rumble_profile", out.input.rumble_profile);
    Read(v, "input.prompt_style", out.input.prompt_style);
    Read(v, "network.check_updates", out.network.check_updates);
    Read(v, "network.discord_presence", out.network.discord_presence);
    Read(v, "camera.roll", out.camera.roll);
    Read(v, "camera.third_person", out.camera.third_person);
    Read(v, "game.game_plus", out.game.game_plus);
    Read(v, "audio.volume", out.audio.volume);
    Read(v, "audio.surround", out.audio.surround);
    Read(v, "audio.spatial", out.audio.spatial);
    Read(v, "audio.controller_speaker", out.audio.controller_speaker);
    Read(v, "audio.controller_speaker_volume", out.audio.controller_speaker_volume);
    Read(v, "voice.microphone_enabled", out.voice.microphone_enabled);
    Read(v, "voice.device", out.voice.device);
    Read(v, "voice.key", out.voice.key);
    Read(v, "upscaling.upscaler", out.upscaling.upscaler);
    Read(v, "upscaling.quality", out.upscaling.quality);
    Read(v, "upscaling.scale", out.upscaling.scale);
    Read(v, "upscaling.sharpness", out.upscaling.sharpness);
    Read(v, "upscaling.dlss_model", out.upscaling.dlss_model);
    Read(v, "upscaling.frame_generation", out.upscaling.frame_generation);
    Read(v, "upscaling.ray_reconstruction", out.upscaling.ray_reconstruction);
    Read(v, "upscaling.neural_rendering", out.upscaling.neural_rendering);
    Read(v, "upscaling.neural_style", out.upscaling.neural_style);
    Read(v, "upscaling.neural_intensity", out.upscaling.neural_intensity);
    Read(v, "upscaling.neural_scale", out.upscaling.neural_scale);
    Read(v, "upscaling.neural_runtime", out.upscaling.neural_runtime);
    Read(v, "upscaling.neural_route_armed", out.upscaling.neural_route_armed);
    Read(v, "upscaling.dlssg_route_armed", out.upscaling.dlssg_route_armed);
    Read(v, "raytracing.shadows", out.ray_tracing.shadows);
    Read(v, "raytracing.ambient_occlusion", out.ray_tracing.ambient_occlusion);
    Read(v, "raytracing.contact_shadows", out.ray_tracing.contact_shadows);
    Read(v, "raytracing.reflections", out.ray_tracing.reflections);
    Read(v, "raytracing.full_shadows", out.ray_tracing.full_shadows);
    Read(v, "raytracing.bounce_light", out.ray_tracing.bounce_light);
    Read(v, "raytracing.glossy_reflections", out.ray_tracing.glossy_reflections);
    Read(v, "graphics.anisotropy", out.graphics.anisotropy);
    Read(v, "graphics.enhanced_textures", out.graphics.enhanced_textures);
    Read(v, "graphics.shadow_quality", out.graphics.shadow_quality);
    Read(v, "graphics.ambient_occlusion", out.graphics.ambient_occlusion);
    Read(v, "graphics.reflections", out.graphics.reflections);
    Read(v, "graphics.bloom", out.graphics.bloom);
    Read(v, "graphics.depth_of_field", out.graphics.depth_of_field);
    Read(v, "graphics.motion_blur", out.graphics.motion_blur);
    Read(v, "graphics.lens_distortion", out.graphics.lens_distortion);
    Read(v, "graphics.lens_ghosts", out.graphics.lens_ghosts);
    Read(v, "graphics.deband", out.graphics.deband);
    Read(v, "graphics.film_grain", out.graphics.film_grain);
    Read(v, "graphics.clarity", out.graphics.clarity);
    Read(v, "graphics.texture_detail", out.graphics.texture_detail);
    Read(v, "graphics.ray_quality", out.graphics.ray_quality);
    if (!v.contains("graphics.lens_ghosts")) {
        AppSettings probe = out;
        probe.graphics.lens_ghosts = true;
        out.graphics.lens_ghosts = DetectGraphicsPreset(probe, false) == GraphicsPreset::Original ||
                                   DetectGraphicsPreset(probe, true) == GraphicsPreset::Original;
    }
    if (!v.contains("graphics.deband")) {
        AppSettings probe = out;
        probe.graphics.deband = false;
        out.graphics.deband = !(DetectGraphicsPreset(probe, false) == GraphicsPreset::Original ||
                                DetectGraphicsPreset(probe, true) == GraphicsPreset::Original);
    }
    Read(v, "vr.enabled", out.vr.enabled);
    Read(v, "vr.flashlight", out.vr.flashlight);
    Read(v, "vr.flashlight_hand", out.vr.flashlight_hand);
    Read(v, "vr.turn", out.vr.turn);
    Read(v, "vr.snap_degrees", out.vr.snap_degrees);
    Read(v, "vr.smooth_speed", out.vr.smooth_speed);
    Read(v, "vr.resolution_scale", out.vr.resolution_scale);
    Read(v, "vr.height_offset", out.vr.height_offset);
    Read(v, "vr.world_size", out.vr.world_size);
    if (v.contains("vr.world_scale") && !v.contains("vr.world_size")) {
        LogInfo("settings: vr.world_scale = {} from an older version is not used; the VR world takes its size from the headset "
                "(vr.world_size = 1)", v.at("vr.world_scale"));
    }
    {
        std::string reached;
        Read(v, "progress.loops_reached", reached);
        if (!reached.empty()) {
            try {
                out.progress.loops_reached = static_cast<uint32_t>(std::stoul(reached, nullptr, 16));
            } catch (...) {
                LogWarn("settings: progress.loops_reached = '{}' is not a hex number", reached);
            }
        }
        Read(v, "progress.game_finished", out.progress.game_finished);
        Read(v, "progress.archive", out.progress.archive);
        Read(v, "progress.archive_backfill", out.progress.archive_backfill);
    }
    Read(v, "extras.fast_walk", out.extras.fast_walk);
    Read(v, "accessibility.sound_captions", out.accessibility.sound_captions);
    Read(v, "accessibility.gyro", out.accessibility.gyro);
    Read(v, "accessibility.gyro_sensitivity", out.accessibility.gyro_sensitivity);
    Read(v, "accessibility.gyro_tightening", out.accessibility.gyro_tightening);
    Read(v, "accessibility.field_of_view", out.accessibility.fov_offset);
    out.accessibility.gyro_tightening =
        std::isfinite(out.accessibility.gyro_tightening) ? std::clamp(out.accessibility.gyro_tightening, 0.0f, 10.0f) : 4.0f;
    for (auto it = v.lower_bound("gyro_bias."); it != v.end() && it->first.starts_with("gyro_bias."); ++it) {
        std::istringstream in(it->second);
        std::array<float, 3> bias{};
        if ((in >> bias[0] >> bias[1] >> bias[2]) && std::isfinite(bias[0]) && std::isfinite(bias[1]) && std::isfinite(bias[2]) &&
            std::abs(bias[0]) < 0.35f && std::abs(bias[1]) < 0.35f && std::abs(bias[2]) < 0.35f) {
            out.accessibility.gyro_bias[it->first.substr(10)] = bias;
        } else {
            LogWarn("settings: {} = '{}' is not a gyro offset", it->first, it->second);
        }
    }
    out.accessibility.fov_offset = std::clamp(out.accessibility.fov_offset, -20, 40);
    out.accessibility.gyro = std::clamp(out.accessibility.gyro, 0, 2);
    out.accessibility.gyro_sensitivity =
        std::isfinite(out.accessibility.gyro_sensitivity) ? std::clamp(out.accessibility.gyro_sensitivity, 0.25f, 4.0f) : 1.0f;
    Read(v, "extras.speedrun", out.extras.speedrun);
    Read(v, "extras.livesplit", out.extras.livesplit);
    Read(v, "extras.livesplit_host", out.extras.livesplit_host);
    Read(v, "extras.livesplit_port", out.extras.livesplit_port);
    out.extras.speedrun = std::clamp(out.extras.speedrun, 0, 2);
    if (out.extras.livesplit_port <= 0 || out.extras.livesplit_port > 65535) out.extras.livesplit_port = 16834;
    for (auto it = v.lower_bound("mods."); it != v.end() && it->first.starts_with("mods."); ++it) {
        bool enabled = true;
        Read(v, it->first.c_str(), enabled);
        out.mods[it->first.substr(5)] = enabled;
    }
    out.graphics.shadow_quality=std::clamp(out.graphics.shadow_quality,0,3);
    out.graphics.texture_detail=std::clamp(out.graphics.texture_detail,0,2);
    if (out.graphics.ray_quality == kRayQualityOldUltraIni) {
        out.graphics.ray_quality = 2;
    } else {
        out.graphics.ray_quality=std::clamp(out.graphics.ray_quality,0,4);
        if (out.graphics.ray_quality == 2) {
            out.graphics.ray_quality = 3;
            LogInfo("settings: ray quality Ultra from an older version is now Ultra with every light's traced shadows, bounce light and glossy reflections");
        }
    }
    out.graphics.film_grain=std::isfinite(out.graphics.film_grain)?std::clamp(out.graphics.film_grain,0.0f,1.0f):1.0f;
    out.graphics.clarity=std::isfinite(out.graphics.clarity)?std::clamp(out.graphics.clarity,0.0f,1.0f):0.0f;
    out.display.width = std::clamp(out.display.width, 320, 16384);
    out.display.height = std::clamp(out.display.height, 240, 16384);
    out.display.fullscreen = std::clamp(out.display.fullscreen, 0, 2);
    out.display.fps_limit = out.display.fps_limit <= 0 ? 0 : std::clamp(out.display.fps_limit, 20, 1000);
    out.display.letterbox = std::clamp(out.display.letterbox, 0, 2);
    out.display.subtitle_scale = std::isfinite(out.display.subtitle_scale) ? std::clamp(out.display.subtitle_scale, 1.0f, 2.0f) : 1.0f;
    out.input.mouse_sensitivity = std::clamp(out.input.mouse_sensitivity, 0.05f, 20.0f);
    out.input.gamepad_sensitivity = std::isfinite(out.input.gamepad_sensitivity) ? std::clamp(out.input.gamepad_sensitivity, 0.25f, 4.0f) : 1.0f;
    out.input.gamepad_dead_zone = std::clamp(out.input.gamepad_dead_zone, 0.0f, 0.9f);
    out.input.rumble_profile = std::clamp(out.input.rumble_profile, 0, 1);
    out.input.prompt_style = std::clamp(out.input.prompt_style, 0, 4);
    out.camera.roll = std::clamp(out.camera.roll, 0.0f, 1.0f);
    out.audio.volume = std::clamp(out.audio.volume, 0.0f, 2.0f);
    out.audio.controller_speaker_volume = std::isfinite(out.audio.controller_speaker_volume)
                                              ? std::clamp(out.audio.controller_speaker_volume, 0.0f, 1.0f)
                                              : 0.5f;
    out.upscaling.scale = std::clamp(out.upscaling.scale, 0.25f, 1.0f);
    out.upscaling.sharpness = std::clamp(out.upscaling.sharpness, 0.0f, 1.0f);
    out.ray_tracing.shadows = std::clamp(out.ray_tracing.shadows, 0, 2);
    out.vr.height_offset = std::isfinite(out.vr.height_offset) ? std::clamp(out.vr.height_offset, -0.5f, 0.5f) : 0.0f;
    out.vr.world_size = std::isfinite(out.vr.world_size) ? std::clamp(out.vr.world_size, 0.5f, 2.0f) : 1.0f;
    out.vr.flashlight = std::clamp(out.vr.flashlight, 0, 1);
    out.vr.flashlight_hand = std::clamp(out.vr.flashlight_hand, 0, 1);
    out.vr.turn = std::clamp(out.vr.turn, 0, 1);
    out.vr.snap_degrees = std::isfinite(out.vr.snap_degrees) ? std::clamp(out.vr.snap_degrees, 10.0f, 90.0f) : 30.0f;
    out.vr.smooth_speed = std::isfinite(out.vr.smooth_speed) ? std::clamp(out.vr.smooth_speed, 20.0f, 360.0f) : 90.0f;
    out.vr.resolution_scale = std::isfinite(out.vr.resolution_scale) ? std::clamp(out.vr.resolution_scale, 0.5f, 2.0f) : 1.0f;
    const int anisotropy = out.graphics.anisotropy;
    out.graphics.anisotropy = anisotropy >= 16 ? 16 : anisotropy >= 8 ? 8 : anisotropy >= 4 ? 4 : anisotropy >= 2 ? 2 : 0;
    return true;
}

bool SaveAppSettings(const std::filesystem::path& path, const AppSettings& s) {
    std::ostringstream text;
    text << "; P.T. port settings. The in-game options (brightness, subtitles, camera inversion) live in the save data.\n\n"
         << "[display]\n"
         << "width = " << s.display.width << "\n"
         << "height = " << s.display.height << "\n"
         << "; 0 window, 1 borderless fullscreen, 2 exclusive fullscreen at the desktop mode (Alt+Enter toggles)\n"
         << "fullscreen = " << s.display.fullscreen << "\n"
         << "vsync = " << (s.display.vsync ? 1 : 0) << "\n"
         << "; open the game's pause menu when the window goes to the background (frozen where the game cannot pause)\n"
         << "pause_on_focus_loss = " << (s.display.pause_on_focus_loss ? 1 : 0) << "\n"
         << "mute_in_background = " << (s.display.mute_in_background ? 1 : 0) << "\n"
         << "; frames per second at most, 0 for no limit (menus and the paused game stay at 60 or less either way)\n"
         << "fps_limit = " << s.display.fps_limit << "\n"
         << "hdr = " << (s.display.hdr ? 1 : 0) << "\n"
         << "; HDR output levels in nits: paper white (80 to 500) is the picture's white and the menus, the peak (400 to 4000) is how bright\n"
         << "; the lamps, the flashlight and the glow around them go; a peak of 0 takes the display's own, 1000 where Windows does not tell it\n"
         << "hdr_paper_white = " << s.display.hdr_paper_white << "\n"
         << "hdr_peak = " << s.display.hdr_peak << "\n"
         << "; black bars over and under the picture in play and in the cutscenes, not in the original: 0 off, 1 2.39:1, 2 1.85:1\n"
         << "letterbox = " << s.display.letterbox << "\n"
         << "; subtitle size relative to the original, 1 to 2\n"
         << "subtitle_scale = " << s.display.subtitle_scale << "\n"
         << "; the graphics API the game draws with at the next start: vulkan, or d3d12 (DirectX 12, Windows)\n"
         << "rendering_api = " << s.display.rendering_api << "\n"
         << "rendering_api_asked = " << (s.display.rendering_api_asked ? 1 : 0) << "\n"
         << "; an earlier start died while DirectX 12 was starting (1 its check, 2 its device, 3 its first frames): DirectX 12 stays\n"
         << "; off until a start with --backend d3d12 draws its first frames; 0 never\n"
         << "d3d12_crashed = " << s.display.d3d12_crashed << "\n\n"
         << "[input]\n"
         << "mouse_sensitivity = " << s.input.mouse_sensitivity << "\n"
         << "; gamepad turn speed after the original stick response, 1 keeps the original\n"
         << "gamepad_sensitivity = " << s.input.gamepad_sensitivity << "\n"
         << "; per stick axis, as the original reads its pads (0.102 is the DualShock 4 dead zone)\n"
         << "gamepad_dead_zone = " << s.input.gamepad_dead_zone << "\n"
         << "; pad vibration from the game's Wwise motion sounds, 0 turns it off\n"
         << "rumble = " << (s.input.rumble ? 1 : 0) << "\n"
         << "; 0 original motor rumble only, 1 also enables supported trigger rumble and DualSense USB cry haptics\n"
         << "rumble_profile = " << s.input.rumble_profile << "\n"
         << "; controller button prompts: 0 follow the pad in use, 1 PlayStation, 2 Xbox, 3 Nintendo, 4 Steam\n"
         << "prompt_style = " << s.input.prompt_style << "\n\n"
         << "[camera]\n"
         << "; share of the head's roll the camera takes: 1 as the original (it leans with the walk, most when strafing right), 0 keeps it level\n"
         << "roll = " << s.camera.roll << "\n"
         << "; experimental third person view (Extras), not in the original: the camera behind the shoulder in free play, first\n"
         << "; person for the zoom and the cutscenes; 0 keeps the original's first person view\n"
         << "third_person = " << (s.camera.third_person ? 1 : 0) << "\n\n"
         << "[game]\n"
         << "; Game+, shown in the options after the first finished game: 1 plays the content the finishes unlocked, 0 plays the original\n"
         << "; game (the finishes and the tier stay saved); a change applies from the next loop\n"
         << "game_plus = " << (s.game.game_plus ? 1 : 0) << "\n\n"
         << "[audio]\n"
         << "volume = " << s.audio.volume << "\n"
         << "surround = " << (s.audio.surround ? 1 : 0) << "\n"
         << "; 1: the 7.1 mix through Windows spatial sound (Windows Sonic, Dolby Atmos, DTS:X) when Windows has one on for the device\n"
         << "spatial = " << (s.audio.spatial ? 1 : 0) << "\n\n"
         << "controller_speaker = " << (s.audio.controller_speaker ? 1 : 0) << "\n"
         << "; separate controller-speaker level, 0 to 1; does not change game volume or DualSense haptics\n"
         << "controller_speaker_volume = " << s.audio.controller_speaker_volume << "\n\n"
         << "[network]\n"
         << "; at start, ask the release page whether a newer version exists (a small note in the PC settings); 0 sends nothing\n"
         << "check_updates = " << (s.network.check_updates ? 1 : 0) << "\n"
         << "; show the loop you are in on your Discord profile (Rich Presence, through the Discord app on this PC); 0 off\n"
         << "discord_presence = " << (s.network.discord_presence ? 1 : 0) << "\n\n"
         << "[voice]\n"
         << "; the microphone listens only where the original game does (f160)\n"
         << "; 0 disables microphone input and its test; the optional key/controller trigger still works\n"
         << "microphone_enabled = " << (s.voice.microphone_enabled ? 1 : 0) << "\n"
         << "; part of the recording device name, empty for the system default\n"
         << "device = \"" << s.voice.device << "\"\n"
         << "; optional key name (SDL scancode name, e.g. J) that stands in for saying the word, empty to disable\n"
         << "key = \"" << s.voice.key << "\"\n\n"
         << "[upscaling]\n"
         << "; off, fsr3 (AMD FSR 3; fsr reads as fsr3), fsr4 (AMD FSR 4, needs a FidelityFX Vulkan build with FSR 4), dlss (NVIDIA DLSS,\n"
         << "; RTX GPUs), xess (Intel XeSS), metalfx (Apple MetalFX, macOS only); off renders at the display resolution like the\n"
         << "; original\n"
         << "upscaler = " << s.upscaling.upscaler << "\n"
         << "; native (anti-aliasing only: FSR native AA, DLAA, XeSS AA), quality, balanced, performance, ultra_performance, custom\n"
         << "quality = " << s.upscaling.quality << "\n"
         << "; render scale per axis for quality = custom (0.25 to 1.0)\n"
         << "scale = " << s.upscaling.scale << "\n"
         << "; 0 to 1, FSR sharpening (RCAS) after the upscaler\n"
         << "sharpness = " << s.upscaling.sharpness << "\n"
         << "; DLSS model: auto (NVIDIA's default for each quality), k, l or m (l and m cost about twice as much on RTX 20 and 30)\n"
         << "dlss_model = " << s.upscaling.dlss_model << "\n"
         << "; frame generation, a generated frame between two rendered frames (needs an upscaler, any quality): off, fsr3 (AMD FSR 3),\n"
         << "; dlss (NVIDIA DLSS Frame Generation, experimental: RTX 40 or newer, starts with the next launch),\n"
         << "; metalfx (Apple MetalFX, macOS 26 or newer, always with v-sync); 1 reads as fsr3\n"
         << "frame_generation = " << s.upscaling.frame_generation << "\n"
         << "; NVIDIA DLSS Ray Reconstruction with the Path traced ray quality and dlss (RTX GPUs): 1 it denoises and upscales the path\n"
         << "; traced light, 0 the port's denoiser and DLSS Super Resolution\n"
         << "ray_reconstruction = " << (s.upscaling.ray_reconstruction ? 1 : 0) << "\n"
         << "; NVIDIA DLSS 5 neural rendering: off or dlss5 (needs an RTX 50 GPU and a runtime NVIDIA publishes for other games;\n"
         << "; none exists yet, so it stays off), neural_style default, natural or cinematic, neural_intensity 0.25 to 1\n"
         << "neural_rendering = " << s.upscaling.neural_rendering << "\n"
         << "neural_style = " << s.upscaling.neural_style << "\n"
         << "neural_intensity = " << s.upscaling.neural_intensity << "\n"
         << "; neural rendering input scale (0.5 to 1): a smaller input for the model where it is too slow (RTX 20 and 30)\n"
         << "neural_scale = " << s.upscaling.neural_scale << "\n"
         << "; a path to the player's nvngx_dlssnr.dll (a file or its folder) when it is not next to pt.exe; never shipped\n"
         << "neural_runtime = " << s.upscaling.neural_runtime << "\n"
         << "; set while the community DLSS 5 route runs, cleared on a clean exit; a start that finds it set keeps DLSS 5 off (it crashed last time)\n"
         << "neural_route_armed = " << (s.upscaling.neural_route_armed ? "yes" : "no") << "\n"
         << "; set while the community DLSS Frame Generation unlock runs, cleared on a clean exit; a start that finds it set keeps FG off (it crashed last time)\n"
         << "dlssg_route_armed = " << (s.upscaling.dlssg_route_armed ? "yes" : "no") << "\n";
    text << "\n[raytracing]\n"
         << "; ray traced shadows in place of the shadow maps, not in the original: 0 off, 1 sharp (as the shadow maps), 2 soft\n"
         << "; (penumbrae from each light's size); needs a GPU with Vulkan ray queries\n"
         << "; changes apply during play on supported devices\n"
         << "shadows = " << s.ray_tracing.shadows << "\n"
         << "; ray traced ambient occlusion: the probe ambient darkened in corners, under and behind objects (same rules)\n"
         << "ambient_occlusion = " << (s.ray_tracing.ambient_occlusion ? 1 : 0) << "\n"
         << "; ray traced contact shadows: what touches or nearly touches a surface shadows it, for every light (same rules)\n"
         << "contact_shadows = " << (s.ray_tracing.contact_shadows ? 1 : 0) << "\n"
         << "; experiment: the floors' local reflections traced against the scene, so they also show what is off screen (same rules)\n"
         << "reflections = " << (s.ray_tracing.reflections ? 1 : 0) << "\n"
         << "; the parts of the Ultra ray quality ([graphics] ray_quality = 3), each 0 or 1: traced shadows from every light, not only\n"
         << "; the ones the original shadows; one bounce of the lights' diffuse light; traced reflections on glossy walls and props\n"
         << "full_shadows = " << (s.ray_tracing.full_shadows ? 1 : 0) << "\n"
         << "bounce_light = " << (s.ray_tracing.bounce_light ? 1 : 0) << "\n"
         << "glossy_reflections = " << (s.ray_tracing.glossy_reflections ? 1 : 0) << "\n";
    text << "\n[graphics]\n"
         << "; not in the original; 0 keeps the original's look\n"
         << "; anisotropic filtering of the textures: 0 (the original's trilinear filtering), 2, 4, 8 or 16\n"
         << "anisotropy = " << s.graphics.anisotropy << "\n"
         << "; textures upscaled from the game files by the bundled upscaler, generated once into the user data folder\n"
         << "enhanced_textures = " << (s.graphics.enhanced_textures ? 1 : 0) << "\n"
         << "shadow_quality = " << s.graphics.shadow_quality << "\n"
         << "ambient_occlusion = " << s.graphics.ambient_occlusion << "\n"
         << "reflections = " << s.graphics.reflections << "\n"
         << "bloom = " << s.graphics.bloom << "\n"
         << "depth_of_field = " << s.graphics.depth_of_field << "\n"
         << "motion_blur = " << s.graphics.motion_blur << "\n"
         << "lens_distortion = " << s.graphics.lens_distortion << "\n"
         << "; the lens flares' large reflections across the screen near a lamp, as in the original; 0 leaves them out\n"
         << "lens_ghosts = " << s.graphics.lens_ghosts << "\n"
         << "; the sub-LSB dither at the tonemap, the colour grading and the final image, against banding in a lamp's glow; 0 is the original's (PS4) output\n"
         << "deband = " << s.graphics.deband << "\n"
         << "film_grain = " << s.graphics.film_grain << "\n"
         << "clarity = " << s.graphics.clarity << "\n"
         << "texture_detail = " << s.graphics.texture_detail << "\n"
         << "; ray quality: 0 balanced (2 rays for the soft shadows and the traced ambient occlusion), 1 high (4), 3 ultra (8 rays,\n"
         << "; traced shadows from every light, bounce light and glossy reflections, the [raytracing] full_* parts), 4 path traced\n"
         << "; (the lights sampled per pixel with traced shadows, bounces of light in place of the probe ambient; no preset uses it);\n"
         << "; 2 reads as 3; 5 is the older Ultra (8 rays alone), for comparisons only\n"
         << "ray_quality = " << (s.graphics.ray_quality == 2 ? kRayQualityOldUltraIni : s.graphics.ray_quality) << "\n";
    text << "\n[vr]\n"
         << "; experimental, untested on a real headset (docs/vr.md); needs an OpenXR runtime; applies at the next start\n"
         << "enabled = " << (s.vr.enabled ? 1 : 0) << "\n"
         << "; the flashlight: 0 follows the head, 1 the controller (flashlight_hand: 0 left, 1 right)\n"
         << "flashlight = " << s.vr.flashlight << "\n"
         << "flashlight_hand = " << s.vr.flashlight_hand << "\n"
         << "; turning with the right stick: 0 snap turns of snap_degrees, 1 smooth at smooth_speed degrees a second\n"
         << "turn = " << s.vr.turn << "\n"
         << "snap_degrees = " << s.vr.snap_degrees << "\n"
         << "smooth_speed = " << s.vr.smooth_speed << "\n"
         << "; the eye images' size against the headset's recommendation (0.5 to 2)\n"
         << "resolution_scale = " << s.vr.resolution_scale << "\n"
         << "; the eye anchor moved up or down, metres (-0.5 to 0.5)\n"
         << "height_offset = " << s.vr.height_offset << "\n"
         << "; the world's size: 1 as the headset measures the player (the eye above its floor), 2 twice as large, 0.5 half\n"
         << "world_size = " << s.vr.world_size << "\n";
    text << "\n[progress]\n"
         << "; the loop browser's unlocks (release builds): the entries reached in play (hex bits) and the game finished once\n"
         << "loops_reached = " << std::hex << s.progress.loops_reached << std::dec << "\n"
         << "game_finished = " << (s.progress.game_finished ? 1 : 0) << "\n"
         << "; the Archive's unlocks (release builds): what play reached, by the entries' unlock keys (src/game/archive.cpp)\n"
         << "archive = " << s.progress.archive << "\n"
         << "; 1: a finished profile was given the museum's keys every finished game reaches (done once)\n"
         << "archive_backfill = " << s.progress.archive_backfill << "\n";
    text << "\n[accessibility]\n"
         << "; not in the original; 0 keeps the original game\n"
         << "; a caption for the sounds you need to hear (a baby laughing, a phone ringing, steps behind you), with a side hint and a short\n"
         << "; rumble where vibration is on; the game's own subtitles are the options screen's\n"
         << "sound_captions = " << (s.accessibility.sound_captions ? 1 : 0) << "\n"
         << "; the controller's motion sensor (DualSense, DualShock 4, Switch Pro): 0 off, 1 turns the camera, 2 aims the flashlight apart\n"
         << "; from the view (it comes back to the middle when the pad is still or L3 is pressed)\n"
         << "gyro = " << s.accessibility.gyro << "\n"
         << "; 0.25 to 4, 1 turns the view or the light as far as the controller turns\n"
         << "gyro_sensitivity = " << s.accessibility.gyro_sensitivity << "\n"
         << "; the gyro tightening, 0 to 10: a turn slower than this many degrees a second is scaled down by its share of it, so a resting\n"
         << "; controller's last jitter fades out; 0 is off. The gyro also learns its resting offset by itself while the controller rests\n"
         << "gyro_tightening = " << s.accessibility.gyro_tightening << "\n"
         << "; degrees added to the vertical angle of the view while you walk (the original's is 54.9, about 85 across at 16:9): -20 to 40;\n"
         << "; the menu offers -10, 0, 10, 20 and 30. The zoom, cutscenes, the peephole, photo mode, third person and VR keep their own view\n"
         << "field_of_view = " << s.accessibility.fov_offset << "\n";
    text << "\n[extras]\n"
         << "; not in the original; 0 keeps the original game\n"
         << "; hold either Shift or the gamepad's bottom face button to walk 1.5x faster; game time is unchanged\n"
         << "fast_walk = " << (s.extras.fast_walk ? 1 : 0) << "\n"
         << "; speedrun timer: 0 off, 1 real time (everything counts), 2 game time (no pauses, no loads); records in this folder\n"
         << "speedrun = " << s.extras.speedrun << "\n"
         << "; LiveSplit: send the start, the splits and the game time to its TCP server (Control > Start TCP Server)\n"
         << "livesplit = " << (s.extras.livesplit ? 1 : 0) << "\n"
         << "livesplit_host = " << s.extras.livesplit_host << "\n"
         << "livesplit_port = " << s.extras.livesplit_port << "\n";
    if (!s.accessibility.gyro_bias.empty()) {
        text << "\n[gyro_bias]\n"
             << "; the resting offset (rad/s, x y z) of each controller calibrated in Accessibility > Calibrate gyro, by SDL joystick GUID\n";
        for (const auto& [guid, bias] : s.accessibility.gyro_bias) {
            text << guid << " = " << bias[0] << " " << bias[1] << " " << bias[2] << "\n";
        }
    }
    if (!s.mods.empty()) {
        text << "\n[mods]\n"
             << "; a folder in mods/ = 1 on, 0 off; applies at the next start\n";
        for (const auto& [folder, enabled] : s.mods) {
            text << folder << " = " << (enabled ? 1 : 0) << "\n";
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        LogWarn("settings: cannot write {}", pt::os::PathToUtf8(path));
        return false;
    }
    out << text.str();
    return true;
}

}
