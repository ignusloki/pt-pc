#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/platform/display_modes.h"
#include "engine/platform/display_policy.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using pt::display::kBorderless;
using pt::display::kExclusive;
using pt::display::kWindow;

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
};

std::vector<glm::ivec2> XwaylandModes(glm::ivec2 output) {
    const glm::ivec2 table[] = {{2048, 1536}, {1920, 1440}, {1600, 1200}, {1440, 1080}, {1400, 1050}, {1280, 1024}, {1280, 960},
                                {1152, 864},  {1024, 768},  {800, 600},   {640, 480},   {2560, 1600}, {1920, 1200}, {1680, 1050},
                                {1440, 900},  {1280, 800},  {720, 480},   {640, 400},   {3840, 2160}, {2560, 1440}, {1920, 1080},
                                {1600, 900},  {1368, 768},  {1280, 720},  {1024, 576}, {864, 486},   {720, 400},   {640, 350}};
    std::vector<glm::ivec2> modes{output};
    for (const glm::ivec2 m : table) {
        if (m.x <= output.x && m.y <= output.y) modes.push_back(m);
    }
    return pt::UniqueDisplaySizes(modes);
}

Rect Fit(glm::dvec2 inner, glm::dvec2 outer) {
    const double s = std::min(outer.x / inner.x, outer.y / inner.y);
    return {(outer.x - inner.x * s) * 0.5, (outer.y - inner.y * s) * 0.5, inner.x * s, inner.y * s};
}

struct Session {
    const char* name;
    bool game_mode;
    glm::ivec2 output;
    glm::ivec2 x_screen;
    double refresh;
};

struct Ini {
    int mode;
    glm::ivec2 size;
};

struct Shown {
    glm::ivec2 window;
    glm::ivec2 render;
    Rect picture;
};

Shown Show(const Session& s, Ini ini, bool old_logic, bool chosen = false) {
    pt::display::Environment env;
    env.gamescope = s.game_mode;
    const int mode = old_logic ? ini.mode : pt::display::PlanMode(env, ini.mode, chosen).mode;
    glm::ivec2 mode_size = ini.size;
    if (mode == kExclusive) {
        mode_size = pt::ClosestDisplaySize(s.game_mode ? XwaylandModes(s.x_screen) : std::vector<glm::ivec2>{s.output}, ini.size);
    }
    glm::ivec2 window = ini.size;
    if (s.game_mode) {
        if (mode != kWindow) window = s.x_screen;
    } else if (mode == kBorderless) {
        window = s.output;
    } else if (mode == kExclusive) {
        window = mode_size;
    }
    const glm::ivec2 render = old_logic ? (mode == kExclusive ? mode_size : window) : pt::display::RenderSize(mode, window, ini.size);
    const Rect in_window = Fit(glm::dvec2(render), glm::dvec2(window));
    Rect on_screen;
    if (s.game_mode) {
        const double fit_nested = std::min(double(s.x_screen.x) / window.x, double(s.x_screen.y) / window.y);
        const double fit_output = std::min(double(s.output.x) / s.x_screen.x, double(s.output.y) / s.x_screen.y);
        const double scale = fit_nested * fit_output;
        const double ox = (s.output.x - window.x * scale) * 0.5;
        const double oy = (s.output.y - window.y * scale) * 0.5;
        on_screen = {ox + in_window.x * scale, oy + in_window.y * scale, in_window.w * scale, in_window.h * scale};
    } else if (mode == kWindow) {
        on_screen = {(s.output.x - window.x) * 0.5 + in_window.x, (s.output.y - window.y) * 0.5 + in_window.y, in_window.w, in_window.h};
    } else {
        const Rect w = Fit(glm::dvec2(window), glm::dvec2(s.output));
        const double scale = w.w / window.x;
        on_screen = {w.x + in_window.x * scale, w.y + in_window.y * scale, in_window.w * scale, in_window.h * scale};
    }
    return {window, render, on_screen};
}

bool Full(const Rect& r, glm::ivec2 output) {
    return std::abs(r.w - output.x) < 0.5 && std::abs(r.h - output.y) < 0.5;
}

void Row(const Session& s, Ini ini, const char* what) {
    const Shown before = Show(s, ini, true);
    const Shown after = Show(s, ini, false);
    std::printf("%-34s %3.0f Hz %-26s | before: window %4dx%-4d render %4dx%-4d picture %6.1fx%-6.1f bars %4.1f/%4.1f | after: window %4dx%-4d "
                "render %4dx%-4d picture %6.1fx%-6.1f bars %4.1f/%4.1f\n",
                s.name, s.refresh, what, before.window.x, before.window.y, before.render.x, before.render.y, before.picture.w, before.picture.h,
                before.picture.x, before.picture.y, after.window.x, after.window.y, after.render.x, after.render.y, after.picture.w,
                after.picture.h, after.picture.x, after.picture.y);
}

}

int main() {
    {
        const auto env_of = [](std::map<std::string, std::string> vars) {
            return pt::display::DetectEnvironment([vars](const char* name) {
                const auto it = vars.find(name);
                return it == vars.end() ? std::string() : it->second;
            });
        };
        assert(env_of({{"GAMESCOPE_WAYLAND_DISPLAY", "gamescope-0"}, {"DISPLAY", ":1"}}).gamescope);
        assert(env_of({{"XDG_CURRENT_DESKTOP", "gamescope"}}).gamescope);
        assert(env_of({{"SteamGamepadUI", "1"}, {"SteamDeck", "1"}}).gamescope);
        assert(!env_of({{"SteamDeck", "1"}, {"XDG_CURRENT_DESKTOP", "KDE"}, {"WAYLAND_DISPLAY", "wayland-0"}}).gamescope);
        assert(!env_of({{"SteamGamepadUI", "1"}, {"SteamDeck", "1"}, {"XDG_CURRENT_DESKTOP", "KDE"}}).gamescope);
        assert(!env_of({}).gamescope);
        assert(env_of({{"GAMESCOPE_WAYLAND_DISPLAY", "gamescope-0"}}).evidence == "GAMESCOPE_WAYLAND_DISPLAY=gamescope-0");
    }
    {
        pt::display::Environment desktop;
        pt::display::Environment gamescope;
        gamescope.gamescope = true;
        for (int mode = 0; mode <= 2; ++mode) {
            assert(pt::display::PlanMode(desktop, mode, false).mode == mode);
            assert(pt::display::PlanMode(desktop, mode, true).mode == mode);
        }
        assert(pt::display::PlanMode(gamescope, kExclusive, false).mode == kBorderless);
        assert(pt::display::PlanMode(gamescope, kExclusive, true).mode == kBorderless);
        assert(pt::display::PlanMode(gamescope, kBorderless, false).mode == kBorderless);
        assert(pt::display::PlanMode(gamescope, kWindow, false).mode == kBorderless);
        assert(pt::display::PlanMode(gamescope, kWindow, true).mode == kWindow);
        assert(*pt::display::PlanMode(gamescope, kExclusive, false).reason);
        assert(!*pt::display::PlanMode(desktop, kExclusive, false).reason);
    }
    assert((pt::display::RenderSize(kExclusive, {1920, 1080}, {1280, 800}) == glm::ivec2(1920, 1080)));
    assert((pt::display::RenderSize(kExclusive, {0, 0}, {1280, 800}) == glm::ivec2(1280, 800)));
    assert((pt::display::RenderSize(kBorderless, {0, 0}, {1280, 800}) == glm::ivec2(0, 0)));
    assert((pt::display::RenderSize(kWindow, {1600, 900}, {1600, 900}) == glm::ivec2(1600, 900)));
    assert((pt::display::RenderSize(kExclusive, {1920, 1080}, {1280, 800}, true) == glm::ivec2(1280, 800)));
    assert((pt::display::RenderSize(kBorderless, {1920, 1080}, {1280, 800}, true) == glm::ivec2(1920, 1080)));
    assert(!pt::display::WindowFits({1600, 900}, {1280, 800}));
    assert(!pt::display::WindowFits({1600, 900}, {1366, 728}));
    assert(pt::display::WindowFits({1600, 900}, {1920, 1040}));
    assert(pt::display::WindowFits({1600, 900}, {0, 0}));
    assert(pt::display::SameShape({1280, 800}, {1920, 1200}));
    assert(!pt::display::SameShape({1280, 800}, {1920, 1080}));
    assert(std::string(pt::display::SteamDeckModel("Galileo\n")) == "Steam Deck OLED");
    assert(std::string(pt::display::SteamDeckModel("Jupiter")) == "Steam Deck LCD");
    assert(pt::display::SteamDeckModel("Some Laptop") == nullptr);

    const glm::ivec2 deck = pt::display::kSteamDeckScreen;
    const Session oled_game_1080{"OLED Game Mode, X screen 1920x1080", true, deck, {1920, 1080}, 90.0};
    const Session oled_game{"OLED Game Mode, X screen 1280x800", true, deck, deck, 90.0};
    const Session lcd_game{"LCD Game Mode, X screen 1280x800", true, deck, deck, 60.0};
    const Session oled_desktop{"OLED Desktop Mode (KDE Wayland)", false, deck, deck, 90.0};
    const Session monitor{"16:9 monitor 1920x1080", false, {1920, 1080}, {1920, 1080}, 60.0};
    const Session ultrawide{"21:9 monitor 2560x1080", false, {2560, 1080}, {2560, 1080}, 60.0};

    {
        const Shown before = Show(oled_game_1080, {kExclusive, {1280, 800}}, true);
        assert((before.window == glm::ivec2(1920, 1080)) && (before.render == glm::ivec2(1280, 800)));
        assert(before.picture.x > 60.0 && before.picture.y > 39.0);
        const Shown after = Show(oled_game_1080, {kExclusive, {1280, 800}}, false);
        assert((after.render == after.window));
        assert(std::abs(after.picture.w - 1280.0) < 0.5 && std::abs(after.picture.h - 720.0) < 0.5);
    }
    for (const Session* s : {&oled_game, &lcd_game}) {
        for (const Ini ini : {Ini{kExclusive, {1280, 800}}, Ini{kExclusive, {1920, 1080}}, Ini{kExclusive, {1280, 720}}, Ini{kBorderless, {1600, 900}},
                              Ini{kWindow, {1600, 900}}, Ini{kWindow, {1280, 800}}}) {
            const Shown after = Show(*s, ini, false);
            assert(Full(after.picture, deck));
            assert((after.render == deck));
        }
        assert((Show(*s, {kWindow, {1600, 900}}, false, true).window == glm::ivec2(1600, 900)));
    }
    for (const Session* s : {&oled_desktop, &monitor, &ultrawide}) {
        for (const Ini ini : {Ini{kExclusive, {1280, 800}}, Ini{kExclusive, {1920, 1080}}, Ini{kBorderless, {1600, 900}}, Ini{kWindow, {1600, 900}},
                              Ini{kWindow, {1280, 720}}}) {
            const Shown before = Show(*s, ini, true);
            const Shown after = Show(*s, ini, false);
            assert((before.window == after.window) && (before.render == after.render));
            assert(before.picture.w == after.picture.w && before.picture.h == after.picture.h);
        }
    }

    std::printf("screen picture: its size on the screen; bars: left/right and top/bottom, in screen pixels\n");
    Row(oled_game_1080, {kExclusive, {1280, 800}}, "pt.ini exclusive 1280x800");
    Row(oled_game_1080, {kExclusive, {1368, 768}}, "pt.ini exclusive 1368x768");
    Row(oled_game_1080, {kExclusive, {1920, 1080}}, "pt.ini exclusive 1920x1080");
    Row(oled_game_1080, {kWindow, {1600, 900}}, "pt.ini window 1600x900");
    for (const Session* s : {&oled_game, &lcd_game}) {
        Row(*s, {kExclusive, {1280, 800}}, "pt.ini exclusive 1280x800");
        Row(*s, {kExclusive, {1280, 720}}, "pt.ini exclusive 1280x720");
        Row(*s, {kBorderless, {1600, 900}}, "pt.ini borderless");
        Row(*s, {kWindow, {1600, 900}}, "pt.ini window 1600x900");
    }
    Row(oled_desktop, {kExclusive, {1280, 800}}, "pt.ini exclusive 1280x800");
    Row(oled_desktop, {kBorderless, {1600, 900}}, "pt.ini borderless");
    Row(monitor, {kExclusive, {1920, 1080}}, "pt.ini exclusive 1920x1080");
    Row(monitor, {kWindow, {1600, 900}}, "pt.ini window 1600x900");
    std::printf("display policy: all checks passed\n");
    return 0;
}
