#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "engine/core/localized_text.h"
#include "engine/platform/rendering_api.h"
#include "engine/platform/settings.h"
#include "game/ui/pc_settings.h"

namespace {

int g_failures = 0;

void Expect(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    g_failures += ok ? 0 : 1;
}

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

}

int main() {
    using pt::RenderingApi;
    RenderingApi api = RenderingApi::Vulkan;
    Expect(pt::ParseRenderingApi("vulkan", api) && api == RenderingApi::Vulkan, "parse vulkan");
    Expect(pt::ParseRenderingApi("D3D12", api) && api == RenderingApi::D3D12, "parse D3D12 (any case)");
    Expect(pt::ParseRenderingApi("DirectX 12", api) && api == RenderingApi::D3D12, "parse DirectX 12");
    Expect(pt::ParseRenderingApi("dx12", api) && api == RenderingApi::D3D12, "parse dx12");
    Expect(!pt::ParseRenderingApi("opengl", api) && !pt::ParseRenderingApi("", api), "refuse opengl and empty");
    Expect(pt::RenderingApiKey(RenderingApi::D3D12) == "d3d12" && pt::RenderingApiKey(RenderingApi::Vulkan) == "vulkan", "pt.ini keys");

    std::string note;
    Expect(pt::ChooseRenderingApi("", "vulkan", note) == RenderingApi::Vulkan && note.empty(), "pt.ini vulkan: Vulkan, no note");
    Expect(pt::ChooseRenderingApi("", "", note) == RenderingApi::Vulkan && note.empty(), "nothing set: Vulkan");
    const bool runs = pt::D3D12RunsGame();
    const RenderingApi cli = pt::ChooseRenderingApi("d3d12", "vulkan", note);
    Expect(runs ? cli == RenderingApi::D3D12 : (cli == RenderingApi::Vulkan && note.find("--backend") != std::string::npos),
           "--backend d3d12: " + (note.empty() ? std::string("DirectX 12") : note));
    const RenderingApi ini = pt::ChooseRenderingApi("", "d3d12", note);
    Expect(runs ? ini == RenderingApi::D3D12 : (ini == RenderingApi::Vulkan && note.find("pt.ini") != std::string::npos),
           "pt.ini d3d12: " + (note.empty() ? std::string("DirectX 12") : note));
    Expect(pt::ChooseRenderingApi("vulkan", "d3d12", note) == RenderingApi::Vulkan && note.empty(), "--backend vulkan wins over pt.ini");
    Expect(pt::ChooseRenderingApi("", "metal", note) == RenderingApi::Vulkan && !note.empty(), "pt.ini garbage: Vulkan with a note");

#if !defined(PT_D3D12_SELECTABLE)
    Expect(!pt::RenderingApiSelectable(), "row and question hidden in this build");
#endif
    SetEnv("PT_RENDERING_API_UI", "1");
#ifdef _WIN32
    Expect(pt::RenderingApiSelectable(), "PT_RENDERING_API_UI=1 shows them on Windows");
#else
    Expect(!pt::RenderingApiSelectable(), "never on Linux or macOS");
#endif
    SetEnv("PT_RENDERING_API_UI", "0");

    const std::filesystem::path ini_path = std::filesystem::temp_directory_path() / "pt_rendering_api_test.ini";
    pt::AppSettings saved;
    saved.display.rendering_api = "d3d12";
    saved.display.rendering_api_asked = true;
    pt::AppSettings loaded;
    Expect(pt::SaveAppSettings(ini_path, saved) && pt::LoadAppSettings(ini_path, loaded) && loaded.display.rendering_api == "d3d12" &&
               loaded.display.rendering_api_asked,
           "pt.ini keeps rendering_api and rendering_api_asked");
    pt::AppSettings defaults;
    Expect(defaults.display.rendering_api == "vulkan" && !defaults.display.rendering_api_asked, "defaults: vulkan, not asked");
    std::filesystem::remove(ini_path);

    pt::AppSettings crashed;
    crashed.display.d3d12_crashed = 3;
    pt::AppSettings crashed_loaded;
    Expect(defaults.display.d3d12_crashed == 0 && pt::SaveAppSettings(ini_path, crashed) && pt::LoadAppSettings(ini_path, crashed_loaded) &&
               crashed_loaded.display.d3d12_crashed == 3,
           "pt.ini keeps d3d12_crashed, default 0");
    std::filesystem::remove(ini_path);
    {
        static int probes = 0;
        pt::SetD3D12RunsGame(true);
        pt::SetD3D12Probe([](std::string&) { ++probes; return true; });
        pt::BlockD3D12("blocked for the test");
        std::string reason;
        Expect(!pt::D3D12Usable(reason) && reason == "blocked for the test" && probes == 0, "BlockD3D12: not usable, with its reason, no probe");
        const RenderingApi blocked = pt::ChooseRenderingApi("", "d3d12", note);
        Expect(blocked == RenderingApi::Vulkan && note.find("blocked for the test") != std::string::npos,
               "BlockD3D12: pt.ini d3d12 starts on Vulkan: " + note);
        pt::SetD3D12Probe([](std::string& r) { ++probes; r = "lacks shader model 6.6"; return false; });
        Expect(!pt::D3D12Usable(reason) && reason == "lacks shader model 6.6" && probes == 1, "failed probe: not usable, with its reason");
        pt::SetD3D12Probe([](std::string&) { ++probes; return false; });
        pt::SkipD3D12Probe();
        Expect(pt::D3D12Usable(reason) && reason.empty() && probes == 1, "SkipD3D12Probe: usable without running the probe");
        pt::SetD3D12Probe([](std::string&) { ++probes; return true; });
        pt::BlockD3D12("blocked before the skip");
        pt::SkipD3D12Probe();
        Expect(!pt::D3D12Usable(reason) && reason == "blocked before the skip" && probes == 1, "SkipD3D12Probe: a block set first stays");
        pt::SetD3D12Probe(nullptr);
        pt::SetD3D12RunsGame(runs);
    }

    for (const char* key : {"pc_rendering_api", "pc_note_rendering_api", "pc_rendering_api_prompt"}) {
        int missing = 0;
        for (int language = 0; language < 14; ++language) {
            const std::string_view text = pt::game::PcText(key, language);
            const bool own = language == 0 ? text != key : !pt::localized::Menu(key, language).empty();
            missing += own && !text.empty() ? 0 : 1;
        }
        Expect(missing == 0, std::string(key) + " in all 14 languages");
    }
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
