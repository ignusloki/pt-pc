#include "engine/render/vk_layers.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/core/json.h"
#include "engine/core/log.h"
#include "engine/platform/os.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace pt::vk {
namespace {

std::string Lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool ContainsNoCase(std::string_view text, std::string_view word) {
    return !word.empty() && Lower(text).find(Lower(word)) != std::string::npos;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

std::vector<std::string> SplitList(std::string_view text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string_view item = Trim(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (!item.empty()) out.emplace_back(item);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

void EnvMember(const json::Value* object, std::string& name, std::string& value) {
    if (!object || !object->IsObject() || object->object.empty()) return;
    name = object->object.front().key;
    const json::Value& v = object->object.front().value;
    value = v.IsString() ? v.string : std::string();
}

bool AddLayer(const json::Value& layer, std::string_view manifest_path, std::vector<ImplicitLayer>& out) {
    if (!layer.IsObject()) return false;
    ImplicitLayer l;
    l.name = layer.String("name");
    if (l.name.empty()) return false;
    l.manifest = std::string(manifest_path);
    l.library = layer.String("library_path");
    EnvMember(layer.Find("disable_environment"), l.disable_env, l.disable_value);
    EnvMember(layer.Find("enable_environment"), l.enable_env, l.enable_value);
    out.push_back(std::move(l));
    return true;
}

[[maybe_unused]] const char* RealEnv(const char* name) {
    return std::getenv(name);
}

#ifdef _WIN32
void ReadManifest(const std::filesystem::path& path, const char* source, bool off, std::vector<ImplicitLayer>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LogInfo("vulkan: implicit layer manifest {} ({}) cannot be read", os::PathToUtf8(path), source);
        return;
    }
    std::stringstream text;
    text << in.rdbuf();
    const size_t first = out.size();
    std::string error;
    if (!ParseLayerManifest(text.str(), os::PathToUtf8(path), out, &error)) {
        LogInfo("vulkan: implicit layer manifest {} ({}) not understood: {}", os::PathToUtf8(path), source, error);
        return;
    }
    for (size_t i = first; i < out.size(); ++i) {
        out[i].source = source;
        out[i].registry_off = off;
    }
}

void ReadRegistry(HKEY root, const char* source, std::vector<ImplicitLayer>& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers", 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    std::wstring name(32768, L'\0');
    for (DWORD index = 0;; ++index) {
        DWORD name_size = static_cast<DWORD>(name.size());
        DWORD type = 0;
        DWORD data = 0;
        DWORD data_size = sizeof(data);
        const LSTATUS status =
            RegEnumValueW(key, index, name.data(), &name_size, nullptr, &type, reinterpret_cast<BYTE*>(&data), &data_size);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) continue;
        const std::wstring path(name.data(), name_size);
        ReadManifest(std::filesystem::path(path), source, type != REG_DWORD || data != 0, out);
    }
    RegCloseKey(key);
}
#endif

}

bool ParseLayerManifest(std::string_view text, std::string_view manifest_path, std::vector<ImplicitLayer>& out, std::string* error) {
    json::Value root;
    std::string parse_error;
    if (!json::Parse(text, root, &parse_error)) {
        if (error) *error = parse_error.empty() ? "not JSON" : parse_error;
        return false;
    }
    if (!root.IsObject()) {
        if (error) *error = "not a JSON object";
        return false;
    }
    bool any = false;
    if (const json::Value* layer = root.Find("layer")) {
        any |= AddLayer(*layer, manifest_path, out);
    }
    if (const json::Value* layers = root.Find("layers"); layers && layers->IsArray()) {
        for (const json::Value& layer : layers->array) {
            any |= AddLayer(layer, manifest_path, out);
        }
    }
    if (!any && error) *error = "no named layer";
    return any;
}

bool LayerFilterMatches(std::string_view filter, std::string_view layer_name, bool implicit) {
    const std::string f = Lower(Trim(filter));
    const std::string name = Lower(layer_name);
    if (f.empty()) return false;
    if (f == "~all~") return true;
    if (f == "~implicit~") return implicit;
    if (f == "~explicit~") return !implicit;
    const bool head = f.front() == '*';
    const bool tail = f.size() > 1 && f.back() == '*';
    const std::string core = f.substr(head ? 1 : 0, f.size() - (head ? 1 : 0) - (tail ? 1 : 0));
    if (head && tail) return name.find(core) != std::string::npos;
    if (head) return name.size() >= core.size() && name.compare(name.size() - core.size(), core.size(), core) == 0;
    if (tail) return name.compare(0, core.size(), core) == 0;
    return name == core;
}

LayerState ImplicitLayerState(const ImplicitLayer& layer, const EnvLookup& env) {
    if (layer.registry_off) return LayerState::OffInRegistry;
    if (!layer.disable_env.empty() && env(layer.disable_env.c_str())) return LayerState::DisabledByEnv;
    if (const char* filter = env("VK_LOADER_LAYERS_DISABLE")) {
        for (const std::string& entry : SplitList(filter)) {
            if (LayerFilterMatches(entry, layer.name, true)) return LayerState::DisabledByFilter;
        }
    }
    if (!layer.enable_env.empty()) {
        const char* value = env(layer.enable_env.c_str());
        if (!value || layer.enable_value != value) return LayerState::NotEnabled;
    }
    return LayerState::Active;
}

const char* LayerStateName(LayerState state) {
    switch (state) {
        case LayerState::Active: return "active";
        case LayerState::OffInRegistry: return "off in the registry";
        case LayerState::NotEnabled: return "not enabled (its enable_environment is unset)";
        case LayerState::DisabledByEnv: return "disabled by its disable_environment";
        case LayerState::DisabledByFilter: return "disabled by VK_LOADER_LAYERS_DISABLE";
    }
    return "?";
}

std::vector<EnvChange> CrashingLayerEnvChanges(const std::vector<ImplicitLayer>& layers, const EnvLookup& env) {
    std::vector<EnvChange> changes;
    const char* current_filter = env("VK_LOADER_LAYERS_DISABLE");
    std::string filter = current_filter ? current_filter : "";
    std::string filter_why;
    auto add_filter = [&](const std::string& entry, const std::string& why) {
        for (const std::string& existing : SplitList(filter)) {
            if (Lower(existing) == Lower(entry)) return;
        }
        filter += filter.empty() ? entry : "," + entry;
        filter_why += filter_why.empty() ? why : ", " + why;
    };
    auto add_env = [&](const std::string& name, const std::string& value, const std::string& why) {
        if (name.empty()) return;
        for (const EnvChange& c : changes) {
            if (c.name == name) return;
        }
        changes.push_back({name, value.empty() ? "1" : value, why});
    };

    const char* allow = env("PT_ALLOW_RTSS");
    if (!(allow && *allow && *allow != '0')) {
        const std::string why = "RTSS's layer crashed 1.0.1 between vkCreateDevice and the first pipeline";
        if (Lower(filter).find("rtss") == std::string::npos) add_filter("*RTSS*", why);
        add_env("DISABLE_RTSS_LAYER", "1", why);
        for (const ImplicitLayer& l : layers) {
            if (ContainsNoCase(l.name, "RTSS")) add_env(l.disable_env, l.disable_value, why);
        }
    }

    if (const char* words = env("PT_VK_DISABLE_LAYERS")) {
        for (const std::string& word : SplitList(words)) {
            const std::string why = "PT_VK_DISABLE_LAYERS names " + word;
            add_filter("*" + word + "*", why);
            for (const ImplicitLayer& l : layers) {
                if (ContainsNoCase(l.name, word)) add_env(l.disable_env, l.disable_value, why);
            }
        }
    }

    if (filter != (current_filter ? current_filter : "")) {
        changes.push_back({"VK_LOADER_LAYERS_DISABLE", filter, filter_why});
    }
    return changes;
}

std::vector<ImplicitLayer> ReadImplicitLayers() {
    std::vector<ImplicitLayer> out;
#ifdef _WIN32
    ReadRegistry(HKEY_LOCAL_MACHINE, "HKLM", out);
    ReadRegistry(HKEY_CURRENT_USER, "HKCU", out);
    if (const wchar_t* add = _wgetenv(L"VK_ADD_IMPLICIT_LAYER_PATH"); add && *add) {
        std::wstringstream list(add);
        std::wstring item;
        while (std::getline(list, item, L';')) {
            if (item.empty()) continue;
            const std::filesystem::path path(item);
            std::error_code ec;
            if (std::filesystem::is_directory(path, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
                    if (entry.path().extension() == L".json") ReadManifest(entry.path(), "VK_ADD_IMPLICIT_LAYER_PATH", false, out);
                }
            } else if (path.extension() == L".json") {
                ReadManifest(path, "VK_ADD_IMPLICIT_LAYER_PATH", false, out);
            }
        }
    }
#endif
    return out;
}

std::vector<ImplicitLayer> PrepareImplicitLayers() {
    static bool prepared = false;
    static std::vector<ImplicitLayer> layers;
    if (prepared) return layers;
    prepared = true;
    layers = ReadImplicitLayers();
#ifdef _WIN32
    for (const EnvChange& change : CrashingLayerEnvChanges(layers, RealEnv)) {
        _putenv_s(change.name.c_str(), change.value.c_str());
        LogInfo("vulkan: {}={} for this process ({})", change.name, change.value, change.why);
    }
#endif
    return layers;
}

void LogImplicitLayers(const std::vector<ImplicitLayer>& layers) {
#ifdef _WIN32
    if (layers.empty()) {
        LogInfo("vulkan: no implicit layers in the registry");
        return;
    }
    for (const ImplicitLayer& l : layers) {
        const std::string file = os::PathToUtf8(os::PathFromUtf8(l.library).filename());
        const bool loaded = !file.empty() && GetModuleHandleW(os::PathFromUtf8(file).c_str()) != nullptr;
        LogInfo("vulkan: implicit layer {} ({}, {}, {}): {}, {}{}{}", l.name, l.source, l.manifest, l.library.empty() ? "no library" : l.library,
                LayerStateName(ImplicitLayerState(l, RealEnv)), loaded ? "loaded" : "not loaded",
                l.disable_env.empty() ? "" : "; disable_environment ", l.disable_env);
    }
#else
    (void)layers;
#endif
}

}
