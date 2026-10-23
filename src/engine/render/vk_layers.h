#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pt::vk {

struct ImplicitLayer {
    std::string name;
    std::string manifest;
    std::string library;
    std::string disable_env;
    std::string disable_value;
    std::string enable_env;
    std::string enable_value;
    bool registry_off = false;
    std::string source;
};

using EnvLookup = std::function<const char*(const char*)>;

bool ParseLayerManifest(std::string_view text, std::string_view manifest_path, std::vector<ImplicitLayer>& out, std::string* error = nullptr);

enum class LayerState { Active, OffInRegistry, NotEnabled, DisabledByEnv, DisabledByFilter };
LayerState ImplicitLayerState(const ImplicitLayer& layer, const EnvLookup& env);
const char* LayerStateName(LayerState state);

bool LayerFilterMatches(std::string_view filter, std::string_view layer_name, bool implicit);

struct EnvChange {
    std::string name;
    std::string value;
    std::string why;
};

std::vector<EnvChange> CrashingLayerEnvChanges(const std::vector<ImplicitLayer>& layers, const EnvLookup& env);

std::vector<ImplicitLayer> ReadImplicitLayers();

std::vector<ImplicitLayer> PrepareImplicitLayers();

void LogImplicitLayers(const std::vector<ImplicitLayer>& layers);

}
