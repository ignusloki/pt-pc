#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pt::mods {

struct Manifest {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    int priority = 0;
    bool enabled = true;
};

bool ParseManifest(std::string_view json, Manifest& out, std::string* error = nullptr);

struct Mod {
    std::string folder;
    std::filesystem::path root;
    Manifest manifest;
    bool enabled = true;
    bool has_manifest = false;
    bool has_script = false;
    std::vector<std::string> files;

    const std::string& Name() const { return manifest.name.empty() ? folder : manifest.name; }
};

std::string AssetKey(std::string_view path);

bool Wins(const Mod& a, const Mod& b);
void SortByPriority(std::vector<Mod>& mods);

std::vector<Mod> Discover(const std::filesystem::path& dir, std::vector<std::string>* warnings = nullptr);

class OverrideIndex {
public:
    struct Hit {
        std::filesystem::path file;
        uint32_t mod = 0;
    };
    void Build(const std::vector<Mod>& mods);
    const Hit* Find(std::string_view asset_path) const;
    bool Empty() const { return files_.empty(); }
    size_t Size() const { return files_.size(); }

private:
    std::unordered_map<std::string, Hit> files_;
};

struct LevelEdit {
    std::string mod;
    std::string stage;
    std::string model;
    std::string light;
    bool hide = false;
    bool collision = true;
    std::array<float, 3> move{0.0f, 0.0f, 0.0f};
    std::array<float, 3> turn{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::string replace;
    bool off = false;
    std::array<float, 3> tint{1.0f, 1.0f, 1.0f};
    float brightness = 1.0f;
};

struct LevelAdd {
    std::string mod;
    std::string stage;
    std::string model;
    std::string name;
    std::array<float, 3> at{0.0f, 0.0f, 0.0f};
    std::array<float, 3> turn{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::string collision = "box";
};

bool ParseLevel(std::string_view json, const std::string& mod, std::vector<LevelEdit>& edits, std::vector<LevelAdd>& adds,
                std::vector<std::string>* warnings, std::string* error = nullptr);
bool LevelStageMatches(std::string_view pattern, std::string_view package_path);
bool LevelNameMatches(std::string_view pattern, std::string_view name);

struct ModSet {
    std::vector<Mod> mods;
    OverrideIndex index;
    std::vector<LevelEdit> level_edits;
    std::vector<LevelAdd> level_adds;
};

std::unique_ptr<ModSet> Load(const std::filesystem::path& dir, const std::map<std::string, bool>& overrides,
                             std::vector<std::string>* warnings = nullptr);

void SetActive(const ModSet* set);
const ModSet* Active();

std::optional<std::vector<uint8_t>> ReadOverride(std::string_view asset_path);
void RegisterGenerated(std::string asset_path, std::vector<uint8_t> bytes);
std::optional<std::vector<uint8_t>> ReadDiskFile(const std::filesystem::path& path);

}
