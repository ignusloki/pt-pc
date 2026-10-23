#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/vk_layers.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "engine/core/log.h"

using namespace pt::vk;

namespace {

constexpr const char* kBandicam = R"({
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_bandicam_helper",
        "type": "GLOBAL",
        "library_path": ".\\bdcamvk64.dll",
        "api_version": "1.3.0",
        "implementation_version": "1",
        "description": "Bandicam Vulkan helper",
        "disable_environment": { "VK_LAYER_bandicam_helper_DEBUG_1": "1" }
    }
})";

constexpr const char* kSteam = R"({
    "file_format_version" : "1.0.0",
    "layer" : {
        "name": "VK_LAYER_VALVE_steam_overlay",
        "type": "GLOBAL",
        "library_path": ".\\SteamOverlayVulkanLayer64.dll",
        "disable_environment": { "DISABLE_VK_LAYER_VALVE_steam_overlay_1": "1" },
        "enable_environment": { "ENABLE_VK_LAYER_VALVE_steam_overlay_1": "1" }
    }
})";

constexpr const char* kTwo = R"({
    "file_format_version": "1.0.1",
    "layers": [
        { "name": "VK_LAYER_RTSS", "library_path": "RTSSVkLayer64.dll", "disable_environment": { "DISABLE_RTSS_LAYER": "1" } },
        { "name": "VK_LAYER_other", "library_path": "other.dll" }
    ]
})";

struct Env {
    std::map<std::string, std::string> vars;
    EnvLookup Lookup() const {
        return [this](const char* name) -> const char* {
            auto it = vars.find(name);
            return it == vars.end() ? nullptr : it->second.c_str();
        };
    }
};

const EnvChange* Find(const std::vector<EnvChange>& changes, const std::string& name) {
    for (const EnvChange& c : changes) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

}

int main() {
    std::vector<ImplicitLayer> layers;
    assert(ParseLayerManifest(kBandicam, "C:\\Program Files (x86)\\Bandicam\\bdcamvk64.json", layers));
    assert(layers.size() == 1);
    assert(layers[0].name == "VK_LAYER_bandicam_helper");
    assert(layers[0].library == ".\\bdcamvk64.dll");
    assert(layers[0].disable_env == "VK_LAYER_bandicam_helper_DEBUG_1" && layers[0].disable_value == "1");
    assert(layers[0].enable_env.empty());
    assert(ParseLayerManifest(kSteam, "steam.json", layers));
    assert(layers[1].enable_env == "ENABLE_VK_LAYER_VALVE_steam_overlay_1" && layers[1].enable_value == "1");
    assert(ParseLayerManifest(kTwo, "two.json", layers));
    assert(layers.size() == 4 && layers[2].name == "VK_LAYER_RTSS" && layers[3].name == "VK_LAYER_other" && layers[3].disable_env.empty());
    std::string error;
    assert(!ParseLayerManifest("{\"file_format_version\": \"1.0.0\"}", "x.json", layers, &error) && !error.empty());
    assert(!ParseLayerManifest("not json", "x.json", layers, &error));
    assert(layers.size() == 4);

    Env env;
    assert(ImplicitLayerState(layers[0], env.Lookup()) == LayerState::Active);
    assert(ImplicitLayerState(layers[1], env.Lookup()) == LayerState::NotEnabled);
    env.vars["ENABLE_VK_LAYER_VALVE_steam_overlay_1"] = "0";
    assert(ImplicitLayerState(layers[1], env.Lookup()) == LayerState::NotEnabled);
    env.vars["ENABLE_VK_LAYER_VALVE_steam_overlay_1"] = "1";
    assert(ImplicitLayerState(layers[1], env.Lookup()) == LayerState::Active);
    env.vars["DISABLE_VK_LAYER_VALVE_steam_overlay_1"] = "";
    assert(ImplicitLayerState(layers[1], env.Lookup()) == LayerState::DisabledByEnv);
    env.vars["VK_LAYER_bandicam_helper_DEBUG_1"] = "1";
    assert(ImplicitLayerState(layers[0], env.Lookup()) == LayerState::DisabledByEnv);
    env.vars.clear();
    env.vars["VK_LOADER_LAYERS_DISABLE"] = "foo, *Bandicam*";
    assert(ImplicitLayerState(layers[0], env.Lookup()) == LayerState::DisabledByFilter);
    assert(ImplicitLayerState(layers[3], env.Lookup()) == LayerState::Active);
    env.vars["VK_LOADER_LAYERS_DISABLE"] = "~implicit~";
    assert(ImplicitLayerState(layers[3], env.Lookup()) == LayerState::DisabledByFilter);
    ImplicitLayer off = layers[3];
    off.registry_off = true;
    assert(ImplicitLayerState(off, Env{}.Lookup()) == LayerState::OffInRegistry);
    assert(LayerFilterMatches("VK_LAYER_RTSS", "vk_layer_rtss", true));
    assert(LayerFilterMatches("VK_LAYER_*", "VK_LAYER_RTSS", true));
    assert(LayerFilterMatches("*_RTSS", "VK_LAYER_RTSS", true));
    assert(!LayerFilterMatches("*_RTS", "VK_LAYER_RTSS", true));
    assert(!LayerFilterMatches("~explicit~", "VK_LAYER_RTSS", true));
    assert(!LayerFilterMatches("", "VK_LAYER_RTSS", true));

    {
        const std::vector<EnvChange> c = CrashingLayerEnvChanges(layers, Env{}.Lookup());
        assert(Find(c, "DISABLE_RTSS_LAYER") && Find(c, "DISABLE_RTSS_LAYER")->value == "1");
        assert(Find(c, "VK_LOADER_LAYERS_DISABLE") && Find(c, "VK_LOADER_LAYERS_DISABLE")->value == "*RTSS*");
        assert(!Find(c, "VK_LAYER_bandicam_helper_DEBUG_1") && !Find(c, "DISABLE_VK_LAYER_VALVE_steam_overlay_1"));
        assert(c.size() == 2);
    }
    {
        Env e;
        e.vars["VK_LOADER_LAYERS_DISABLE"] = "VK_LAYER_foo";
        const std::vector<EnvChange> c = CrashingLayerEnvChanges(layers, e.Lookup());
        assert(Find(c, "VK_LOADER_LAYERS_DISABLE")->value == "VK_LAYER_foo,*RTSS*");
        e.vars["VK_LOADER_LAYERS_DISABLE"] = "VK_LAYER_RTSS";
        assert(!Find(CrashingLayerEnvChanges(layers, e.Lookup()), "VK_LOADER_LAYERS_DISABLE"));
        e.vars["PT_ALLOW_RTSS"] = "1";
        assert(CrashingLayerEnvChanges(layers, e.Lookup()).empty());
    }
    {
        Env e;
        e.vars["PT_VK_DISABLE_LAYERS"] = "bandicam, steam_overlay";
        const std::vector<EnvChange> c = CrashingLayerEnvChanges(layers, e.Lookup());
        assert(Find(c, "VK_LAYER_bandicam_helper_DEBUG_1") && Find(c, "VK_LAYER_bandicam_helper_DEBUG_1")->value == "1");
        assert(Find(c, "DISABLE_VK_LAYER_VALVE_steam_overlay_1"));
        assert(Find(c, "VK_LOADER_LAYERS_DISABLE")->value == "*RTSS*,*bandicam*,*steam_overlay*");
        assert(Find(c, "VK_LOADER_LAYERS_DISABLE") == &c.back());
    }

#ifdef _WIN32
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "pt_vk_layers_test";
    std::filesystem::create_directories(dir);
    {
        std::ofstream(dir / "bdcamvk64.json") << kBandicam;
    }
    const std::filesystem::path log = dir / "pt.log";
    pt::LogSetFile(log);
    _putenv_s("VK_ADD_IMPLICIT_LAYER_PATH", dir.string().c_str());
    _putenv_s("PT_VK_DISABLE_LAYERS", "bandicam");
    assert(std::getenv("VK_LAYER_bandicam_helper_DEBUG_1") == nullptr);
    const std::vector<ImplicitLayer> found = PrepareImplicitLayers();
    bool mock = false;
    for (const ImplicitLayer& l : found) {
        mock |= l.name == "VK_LAYER_bandicam_helper" && l.source == "VK_ADD_IMPLICIT_LAYER_PATH";
    }
    assert(mock);
    const char* set = std::getenv("VK_LAYER_bandicam_helper_DEBUG_1");
    assert(set && std::string(set) == "1");
    const char* filter = std::getenv("VK_LOADER_LAYERS_DISABLE");
    assert(filter && std::string(filter).find("*bandicam*") != std::string::npos);
    LogImplicitLayers(found);
    pt::LogSetFile({});
    std::stringstream text;
    text << std::ifstream(log).rdbuf();
    assert(text.str().find("vulkan: VK_LAYER_bandicam_helper_DEBUG_1=1 for this process (PT_VK_DISABLE_LAYERS names bandicam)") != std::string::npos);
    assert(text.str().find("vulkan: implicit layer VK_LAYER_bandicam_helper (VK_ADD_IMPLICIT_LAYER_PATH") != std::string::npos);
    assert(text.str().find("disabled by its disable_environment") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
#endif
    std::printf("vk_layers_test: ok\n");
    return 0;
}
