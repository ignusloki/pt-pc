#pragma once

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <thread>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace pt::update_journal {
namespace fs = std::filesystem;

inline constexpr const char* kName = "update-journal.txt";
inline constexpr const char* kHeader = "pt-update-journal 1";

enum class State { Swapping, Swapped, Started };
struct Step {
    char kind = 'r';
    std::string path;
};
struct Journal {
    State state = State::Swapping;
    fs::path install, staging, backup, download;
    std::string old_version, new_version;
    uint64_t pid = 0;
    std::vector<Step> steps;
};

inline std::string ToUtf8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}
inline fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }
inline const char* StateName(State state) { return state == State::Swapping ? "swapping" : state == State::Swapped ? "swapped" : "started"; }

inline bool SafeRelative(const std::string& text) {
    if (text.empty()) return false;
    const fs::path relative = FromUtf8(text);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) return false;
    for (const auto& part : relative) {
        const std::string name = ToUtf8(part);
        if (name.empty() || name == ".." || name == "." || name.find(':') != std::string::npos) return false;
    }
    return true;
}
inline bool OwnFolder(const fs::path& install, const fs::path& dir, const char* prefix) {
    if (dir.empty() || install.empty()) return false;
    const std::string name = ToUtf8(dir.filename());
    return dir.parent_path() == install.parent_path() && name.size() > std::string(prefix).size() && name.starts_with(prefix) &&
           name.find_first_of("/\\") == std::string::npos;
}

inline bool Write(const fs::path& file, const Journal& journal) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    const fs::path temporary = fs::path(file).concat(".new");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << kHeader << "\nstate=" << StateName(journal.state) << "\ninstall=" << ToUtf8(journal.install) << "\nstaging=" << ToUtf8(journal.staging)
            << "\nbackup=" << ToUtf8(journal.backup) << "\ndownload=" << ToUtf8(journal.download) << "\nold_version=" << journal.old_version
            << "\nnew_version=" << journal.new_version << "\npid=" << journal.pid << "\n";
        for (const auto& step : journal.steps) out << "step=" << step.kind << " " << step.path << "\n";
        out.flush();
        if (!out) return false;
    }
    fs::rename(temporary, file, error);
    return !error;
}

inline std::optional<Journal> Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string line;
    if (!in || !std::getline(in, line) || line != kHeader) return std::nullopt;
    Journal journal;
    bool state = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
        if (key == "state") {
            state = true;
            if (value == "swapping") journal.state = State::Swapping;
            else if (value == "swapped") journal.state = State::Swapped;
            else if (value == "started") journal.state = State::Started;
            else return std::nullopt;
        } else if (key == "install") journal.install = FromUtf8(value);
        else if (key == "staging") journal.staging = FromUtf8(value);
        else if (key == "backup") journal.backup = FromUtf8(value);
        else if (key == "download") journal.download = FromUtf8(value);
        else if (key == "old_version") journal.old_version = value;
        else if (key == "new_version") journal.new_version = value;
        else if (key == "pid") journal.pid = std::strtoull(value.c_str(), nullptr, 10);
        else if (key == "step") {
            if (value.size() < 3 || value[1] != ' ' || (value[0] != 'a' && value[0] != 'r' && value[0] != 'd') || !SafeRelative(value.substr(2))) return std::nullopt;
            journal.steps.push_back({value[0], value.substr(2)});
        }
    }
    std::error_code error;
    const fs::path install = fs::weakly_canonical(fs::absolute(file).parent_path().parent_path(), error);
    if (!state || error || journal.install.empty() || fs::weakly_canonical(journal.install, error) != install) return std::nullopt;
    journal.install = install;
    for (fs::path* dir : {&journal.staging, &journal.backup, &journal.download})
        if (!dir->empty()) *dir = fs::weakly_canonical(*dir, error);
    if (!OwnFolder(install, journal.staging, ".pt-update-") || !OwnFolder(install, journal.backup, ".pt-update-old-")) return std::nullopt;
    if (!journal.download.empty() && !OwnFolder(install, journal.download, ".pt-update-dl-")) return std::nullopt;
    return journal;
}

inline bool SetState(const fs::path& file, State state) {
    auto journal = Read(file);
    if (!journal) return false;
    journal->state = state;
    return Write(file, *journal);
}

inline bool Rollback(const Journal& journal, std::string* error = nullptr) {
    bool ok = true;
    for (auto it = journal.steps.rbegin(); it != journal.steps.rend(); ++it) {
        const fs::path rel = FromUtf8(it->path);
        const fs::path live = journal.install / rel, saved = journal.backup / rel, fresh = journal.staging / rel;
        std::error_code e;
        const bool bundle = it->path == "P.T. PC Port.app" || it->path == "P.T..app";
        const auto movable = [&](const fs::path& path) {
            const auto status = fs::symlink_status(path, e);
            return !e && (fs::is_regular_file(status) || (bundle && fs::is_directory(status)));
        };
        const bool have_saved = movable(saved);
        if (it->kind == 'a' || (it->kind == 'r' && have_saved)) {
            if (movable(live) && !fs::exists(fresh, e)) {
                fs::create_directories(fresh.parent_path(), e);
                fs::rename(live, fresh, e);
                if (e) {
                    ok = false;
                    if (error) *error = "could not move " + it->path + " back: " + e.message();
                    continue;
                }
            }
            if (it->kind == 'a')
                for (fs::path dir = live.parent_path(); dir != journal.install && dir.has_relative_path() && fs::is_directory(dir, e) &&
                                                       !fs::is_symlink(dir, e) && fs::directory_iterator(dir, e) == fs::directory_iterator();
                     dir = dir.parent_path())
                    if (!fs::remove(dir, e)) break;
        }
        if ((it->kind == 'r' || it->kind == 'd') && have_saved) {
            e.clear();
            if (fs::exists(live, e)) {
                ok = false;
                if (error) *error = "could not restore " + it->path + ": the new file is still in place";
                continue;
            }
            fs::create_directories(live.parent_path(), e);
            fs::rename(saved, live, e);
            if (e) {
                ok = false;
                if (error) *error = "could not restore " + it->path + ": " + e.message();
            }
        }
    }
    return ok;
}

inline void RemoveFolders(const Journal& journal) {
    std::error_code error;
    for (const auto& [dir, prefix] : {std::pair{journal.staging, ".pt-update-"}, std::pair{journal.backup, ".pt-update-old-"},
                                      std::pair{journal.download, ".pt-update-dl-"}})
        if (OwnFolder(journal.install, dir, prefix) && !fs::is_symlink(dir, error)) {
            for (int attempt = 0; attempt < 30; ++attempt) {
                error.clear();
                fs::remove_all(dir, error);
                if (!error || !fs::exists(dir, error)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
}

inline bool BackupHoldsFiles(const Journal& journal) {
    std::error_code error;
    for (fs::recursive_directory_iterator it(journal.backup, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error)) return true;
    return false;
}

}
