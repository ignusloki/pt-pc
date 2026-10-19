#include <algorithm>
#include <cctype>
#include <format>

#include "engine/core/ascii.h"
#include "engine/core/json.h"
#include "engine/fs/mods.h"

namespace pt::mods {
namespace {

std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(pt::AsciiLower(c));
    return out;
}

bool Vec3(const json::Value& entry, const char* key, std::array<float, 3>& out, std::string& warning) {
    const json::Value* v = entry.Find(key);
    if (!v) return true;
    if (v->IsNumber()) {
        out = {static_cast<float>(v->number), static_cast<float>(v->number), static_cast<float>(v->number)};
        return true;
    }
    if (!v->IsArray() || v->Size() != 3 || !v->At(0)->IsNumber() || !v->At(1)->IsNumber() || !v->At(2)->IsNumber()) {
        warning = std::format("\"{}\" must be [x, y, z]", key);
        return false;
    }
    for (size_t i = 0; i < 3; ++i) out[i] = static_cast<float>(v->At(i)->number);
    return true;
}

}

bool LevelStageMatches(std::string_view pattern, std::string_view package_path) {
    return pattern.empty() || Lower(package_path).find(Lower(pattern)) != std::string::npos;
}

bool LevelNameMatches(std::string_view pattern, std::string_view name) {
    if (pattern.empty()) return false;
    const std::string p = Lower(pattern);
    std::string n = Lower(name);
    if (p.find('|') == std::string::npos) {
        if (const size_t bar = n.find_last_of('|'); bar != std::string::npos) n = n.substr(bar + 1);
    }
    if (p.ends_with('*')) return n.starts_with(std::string_view(p).substr(0, p.size() - 1));
    return n == p;
}

bool ParseLevel(std::string_view text, const std::string& mod, std::vector<LevelEdit>& edits, std::vector<LevelAdd>& adds,
                std::vector<std::string>* warnings, std::string* error) {
    json::Value doc;
    std::string parse_error;
    if (!json::Parse(text, doc, &parse_error)) {
        if (error) *error = "not valid JSON: " + parse_error;
        return false;
    }
    if (!doc.IsObject()) {
        if (error) *error = "not a JSON object";
        return false;
    }
    auto warn = [&](const char* list, size_t i, const std::string& what) {
        if (warnings) warnings->push_back(std::format("{}[{}] skipped: {}", list, i, what));
    };
    for (const auto& [key, value] : doc.object) {
        if (key != "edit" && key != "add" && warnings) warnings->push_back(std::format("unknown key \"{}\" (edit and add are read)", key));
    }
    if (const json::Value* list = doc.Find("edit")) {
        for (size_t i = 0; i < list->Size(); ++i) {
            const json::Value& e = *list->At(i);
            if (!e.IsObject()) {
                warn("edit", i, "not an object");
                continue;
            }
            LevelEdit edit;
            edit.mod = mod;
            edit.stage = e.String("stage");
            edit.model = e.String("model");
            edit.light = e.String("light");
            if (edit.model.empty() == edit.light.empty()) {
                warn("edit", i, "give either \"model\" or \"light\" (the entity's name)");
                continue;
            }
            edit.hide = e.Bool("hide", false);
            edit.collision = e.Bool("collision", true);
            edit.replace = e.String("replace");
            edit.off = e.Bool("off", false);
            edit.brightness = static_cast<float>(e.Number("brightness", 1.0));
            std::string w;
            if (!Vec3(e, "move", edit.move, w) || !Vec3(e, "turn", edit.turn, w) || !Vec3(e, "scale", edit.scale, w) || !Vec3(e, "tint", edit.tint, w)) {
                warn("edit", i, w);
                continue;
            }
            edits.push_back(std::move(edit));
        }
    }
    if (const json::Value* list = doc.Find("add")) {
        for (size_t i = 0; i < list->Size(); ++i) {
            const json::Value& e = *list->At(i);
            if (!e.IsObject()) {
                warn("add", i, "not an object");
                continue;
            }
            LevelAdd add;
            add.mod = mod;
            add.stage = e.String("stage");
            add.model = e.String("model");
            add.name = e.String("name", std::format("{}_add{}", mod, i));
            add.collision = Lower(e.String("collision", "box"));
            if (add.model.empty()) {
                warn("add", i, "\"model\" (a /Assets/... path) is missing");
                continue;
            }
            if (add.stage.empty()) {
                warn("add", i, "\"stage\" is missing (the stage to place it in, e.g. pt14_hallway)");
                continue;
            }
            if (add.collision != "none" && add.collision != "box" && add.collision != "mesh") {
                warn("add", i, "\"collision\" must be \"none\", \"box\" or \"mesh\"");
                continue;
            }
            std::string w;
            if (!Vec3(e, "at", add.at, w) || !Vec3(e, "turn", add.turn, w) || !Vec3(e, "scale", add.scale, w)) {
                warn("add", i, w);
                continue;
            }
            adds.push_back(std::move(add));
        }
    }
    return true;
}

}
