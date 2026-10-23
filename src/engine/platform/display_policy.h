#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

namespace pt::display {

constexpr int kWindow = 0;
constexpr int kBorderless = 1;
constexpr int kExclusive = 2;

inline const char* ModeName(int mode) {
    return mode == kExclusive ? "exclusive fullscreen" : mode == kBorderless ? "borderless fullscreen" : "window";
}

struct Environment {
    bool gamescope = false;
    std::string evidence;
};

inline Environment DetectEnvironment(const std::function<std::string(const char*)>& get) {
    Environment env;
    const auto note = [&](const char* name, const std::string& value) {
        if (!env.evidence.empty()) env.evidence += ", ";
        env.evidence += std::string(name) + "=" + value;
    };
    if (const std::string value = get("GAMESCOPE_WAYLAND_DISPLAY"); !value.empty()) {
        env.gamescope = true;
        note("GAMESCOPE_WAYLAND_DISPLAY", value);
    }
    std::string desktop = get("XDG_CURRENT_DESKTOP");
    std::string lower = desktop;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("gamescope") != std::string::npos) {
        env.gamescope = true;
        note("XDG_CURRENT_DESKTOP", desktop);
    }
    if (get("SteamGamepadUI") == "1" && get("SteamDeck") == "1" && desktop.empty()) {
        env.gamescope = true;
        note("SteamGamepadUI", "1");
        note("SteamDeck", "1");
    }
    return env;
}

struct ModePlan {
    int mode = kWindow;
    const char* reason = "";
};

inline ModePlan PlanMode(const Environment& env, int setting, bool chosen) {
    setting = std::clamp(setting, kWindow, kExclusive);
    if (!env.gamescope) return {setting, ""};
    if (setting == kExclusive) {
        return {kBorderless, "gamescope only emulates an exclusive mode and keeps the window at its screen's size"};
    }
    if (setting == kWindow && !chosen) {
        return {kBorderless, "gamescope scales the one window it shows to fit, a window of another shape than its screen gets bars"};
    }
    return {setting, ""};
}

inline glm::ivec2 RenderSize(int mode, glm::ivec2 window_pixels, glm::ivec2 setting_size, bool minimized = false) {
    const bool has_pixels = window_pixels.x > 0 && window_pixels.y > 0;
    if (mode == kExclusive && (minimized || !has_pixels)) return setting_size;
    return has_pixels ? window_pixels : glm::ivec2(0);
}

inline bool WindowFits(glm::ivec2 window, glm::ivec2 usable) {
    return usable.x <= 0 || usable.y <= 0 || (window.x <= usable.x && window.y <= usable.y);
}

inline bool SameShape(glm::ivec2 a, glm::ivec2 b) {
    if (a.x <= 0 || a.y <= 0 || b.x <= 0 || b.y <= 0) return true;
    const double ra = static_cast<double>(a.x) / a.y;
    const double rb = static_cast<double>(b.x) / b.y;
    return std::abs(ra / rb - 1.0) < 0.01;
}

inline const char* SteamDeckModel(std::string_view product) {
    while (!product.empty() && std::isspace(static_cast<unsigned char>(product.back()))) product.remove_suffix(1);
    if (product == "Jupiter") return "Steam Deck LCD";
    if (product == "Galileo") return "Steam Deck OLED";
    return nullptr;
}
constexpr glm::ivec2 kSteamDeckScreen{1280, 800};

}
