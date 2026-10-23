#pragma once
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/fs/pt_release.h"

namespace pt::game_folder {
namespace fs = std::filesystem;

inline bool LooksLikeGameDir(const fs::path& dir) {
    std::error_code error;
    return !dir.empty() && fs::is_regular_file(dir / "chunk1.psarc", error) && fs::is_regular_file(dir / "texture.qar", error);
}

inline std::vector<std::string> KnownNames() {
    std::vector<std::string> names;
    for (const pt::release::KnownTitle& title : pt::release::kKnownTitles) names.push_back(title.title_id);
    return names;
}

inline std::vector<fs::path> ChildFolders(const fs::path& dir, size_t limit = 256) {
    struct Child {
        int rank;
        std::string name;
        fs::path path;
    };
    std::vector<Child> children;
    std::error_code error;
    const std::vector<std::string> known = KnownNames();
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, error), end; !error && it != end && children.size() < limit;
         it.increment(error)) {
        std::error_code type_error;
        if (!it->is_directory(type_error)) continue;
        const std::u8string wide = it->path().filename().u8string();
        const std::string name(wide.begin(), wide.end());
        if (name == "sce_sys" || name == "sce_module") continue;
        int rank = 1000;
        const std::string title = pt::release::TitleIdOfName(name);
        if (!title.empty()) {
            const auto at = std::find(known.begin(), known.end(), title);
            rank = at != known.end() ? static_cast<int>(at - known.begin()) * 2 + (name == title ? 0 : 1) : 500;
        }
        children.push_back({rank, name, it->path()});
    }
    std::sort(children.begin(), children.end(), [](const Child& a, const Child& b) { return a.rank != b.rank ? a.rank < b.rank : a.name < b.name; });
    std::vector<fs::path> paths;
    for (const Child& child : children) paths.push_back(child.path);
    return paths;
}

inline fs::path FindGameFolder(const fs::path& picked) {
    if (picked.empty()) return {};
    if (LooksLikeGameDir(picked)) return picked;
    std::vector<fs::path> level{picked};
    for (int depth = 0; depth < 2 && !level.empty(); ++depth) {
        std::vector<fs::path> next;
        for (const fs::path& dir : level) {
            for (fs::path& child : ChildFolders(dir)) {
                if (LooksLikeGameDir(child)) return child;
                next.push_back(std::move(child));
            }
        }
        level = std::move(next);
    }
    return {};
}

struct Found {
    fs::path folder;
    std::string where;
};

inline Found FindNearProgram(fs::path dir) {
    auto where = [](int depth) {
        return depth == 0 ? std::string("next to the program") : std::to_string(depth) + (depth == 1 ? " folder" : " folders") + " above the program";
    };
    if (!dir.empty() && dir.filename().empty()) dir = dir.parent_path();
    std::vector<fs::path> levels;
    for (int depth = 0; depth < 5 && !dir.empty(); ++depth) {
        levels.push_back(dir);
        if (dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    const std::vector<std::string> names = KnownNames();
    for (size_t depth = 0; depth < levels.size(); ++depth) {
        for (const std::string& name : names) {
            for (const fs::path& candidate : {levels[depth] / "game" / name, levels[depth] / name}) {
                if (LooksLikeGameDir(candidate)) return {candidate, where(static_cast<int>(depth))};
            }
        }
    }
    for (size_t depth = 0; depth < levels.size(); ++depth) {
        std::vector<fs::path> folders;
        if (LooksLikeGameDir(levels[depth] / "game")) return {levels[depth] / "game", where(static_cast<int>(depth))};
        for (fs::path& child : ChildFolders(levels[depth] / "game")) folders.push_back(std::move(child));
        if (depth == 0)
            for (fs::path& child : ChildFolders(levels[depth])) folders.push_back(std::move(child));
        for (const fs::path& candidate : folders) {
            if (LooksLikeGameDir(candidate)) return {candidate, where(static_cast<int>(depth))};
        }
    }
    return {};
}

}
