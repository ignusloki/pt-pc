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

// User mods (docs/modding.md): folders under mods/ next to pt.exe whose Assets/ tree stands in for the game's /Assets/ files.
// Nothing here runs unless a mod is installed: with no active set every lookup is one null pointer check.
namespace pt::mods {

struct Manifest {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    // higher wins when two mods carry the same file
    int priority = 0;
    bool enabled = true;
};

// mod.json; tolerant of comments, trailing commas, unknown keys and a UTF-8 BOM. False only for text that is not a JSON object.
bool ParseManifest(std::string_view json, Manifest& out, std::string* error = nullptr);

struct Mod {
    // the folder's name under mods/, the mod's identity in pt.ini [mods]
    std::string folder;
    std::filesystem::path root;
    Manifest manifest;
    // the manifest's enabled, then pt.ini [mods] <folder> = 0/1 over it
    bool enabled = true;
    bool has_manifest = false;
    bool has_script = false;
    // files under root/Assets, as paths relative to it with forward slashes
    std::vector<std::string> files;

    const std::string& Name() const { return manifest.name.empty() ? folder : manifest.name; }
};

// Lower case, forward slashes, no /Assets/ (or the archive's as/) prefix: "/Assets/sh/A.fpk" and "as/sh/a.fpk" are both
// "sh/a.fpk". Empty for a path outside /Assets/, which no mod can replace.
std::string AssetKey(std::string_view path);

// The order mods apply in: the winner of a file first (higher priority, then on a tie the folder name that sorts later, so a
// "zz_" or "99_" prefix overrides as in the usual load orders).
bool Wins(const Mod& a, const Mod& b);
void SortByPriority(std::vector<Mod>& mods);

// Every subfolder of `dir`, sorted by SortByPriority; the files are listed, nothing is read but mod.json.
std::vector<Mod> Discover(const std::filesystem::path& dir, std::vector<std::string>* warnings = nullptr);

class OverrideIndex {
public:
    struct Hit {
        std::filesystem::path file;
        uint32_t mod = 0;
    };
    // the enabled mods' files; `mods` in any order
    void Build(const std::vector<Mod>& mods);
    const Hit* Find(std::string_view asset_path) const;
    bool Empty() const { return files_.empty(); }
    size_t Size() const { return files_.size(); }

private:
    std::unordered_map<std::string, Hit> files_;
};

// level.json (docs/modding.md, Levels): changes to the models and lights the game's stages place, and models a mod adds.
// "stage" picks the stage packages by part of their name (empty: every stage); "model" and "light" name the entity, with a
// trailing * matching any end. Positions are in the stage's own space, metres; turns are degrees about X, Y and Z.
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
    // the model drawn instead, a game .fmdl or a mod's .glb/.gltf path
    std::string replace;
    // lights: off, a tint multiplying the colour, a factor of the brightness
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
    // "none", "box" (the model's bounds) or "mesh" (its triangles)
    std::string collision = "box";
};

// a level.json's text, its entries appended to `edits` and `adds` (mod: the folder they come from); false with the reason when
// the text is not a JSON object, a bad entry is skipped with a warning
bool ParseLevel(std::string_view json, const std::string& mod, std::vector<LevelEdit>& edits, std::vector<LevelAdd>& adds,
                std::vector<std::string>* warnings, std::string* error = nullptr);
// "stage" against a stage package path, "model"/"light" against an entity name (case-insensitive, a trailing * any end)
bool LevelStageMatches(std::string_view pattern, std::string_view package_path);
bool LevelNameMatches(std::string_view pattern, std::string_view name);

// The mounted mods: all that were found (for the settings page) and the index of the enabled ones.
struct ModSet {
    std::vector<Mod> mods;
    OverrideIndex index;
    // the enabled mods' level.json entries, in load order (a later entry of the same model applies over an earlier one)
    std::vector<LevelEdit> level_edits;
    std::vector<LevelAdd> level_adds;
};

// Discover + the pt.ini choices + Build. `overrides`: folder -> enabled from pt.ini [mods].
std::unique_ptr<ModSet> Load(const std::filesystem::path& dir, const std::map<std::string, bool>& overrides,
                             std::vector<std::string>* warnings = nullptr);

// The set the file reads consult. Set once at start-up before any loading thread runs and kept to the end; nullptr (the
// default) when no enabled mod has a file, so the game reads exactly what it reads without mods.
void SetActive(const ModSet* set);
const ModSet* Active();

// The bytes of the active mods' replacement of `asset_path`, or nothing (always nothing without mods).
std::optional<std::vector<uint8_t>> ReadOverride(std::string_view asset_path);
// A file made while loading (a glTF model's embedded image), read by ReadOverride at `asset_path` from then on. Thread safe.
void RegisterGenerated(std::string asset_path, std::vector<uint8_t> bytes);
std::optional<std::vector<uint8_t>> ReadDiskFile(const std::filesystem::path& path);

}
