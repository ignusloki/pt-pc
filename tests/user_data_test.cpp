#include "engine/platform/user_data.h"
#include "engine/platform/settings.h"
#include "game/save_data.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using pt::platform::LegacyQuestion;
using pt::platform::PrepareUserDataDirectory;

namespace {
void Write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << value;
}

std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void Age(const fs::path& path, int hours) {
    fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(hours));
}

std::string U8(const fs::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

size_t CountFiles(const fs::path& dir) {
    size_t count = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) count += it->is_regular_file();
    return count;
}

bool OnlyMarkerLeft(const fs::path& appdataPtPort, const char* marker) {
    size_t entries = 0;
    for (const auto& entry : fs::directory_iterator(appdataPtPort)) {
        ++entries;
        if (entry.path().filename() != marker) return false;
    }
    return entries == 1;
}

struct Machine {
    fs::path root;
    fs::path PtPort() const { return root / "appdata" / "pt-port"; }
    fs::path Legacy() const { return PtPort() / "pt"; }
    fs::path Install(int n) const { return root / ("P.T. Silent Hills") / ("pt-port-2026101" + std::to_string(n)) / "data"; }
};

struct FileHold {
    fs::path path;
    bool held = false;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    bool folder = false;
#endif
    FileHold(const fs::path& file, bool deleteOnly) : path(file) {
#ifdef _WIN32
        (void)deleteOnly;
        handle = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        held = handle != INVALID_HANDLE_VALUE;
#else
        held = ::geteuid() != 0;
        folder = deleteOnly;
        if (held && folder) fs::permissions(file.parent_path(), fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        if (held && !folder) fs::permissions(file, fs::perms::none, fs::perm_options::replace);
#endif
    }
    void Release() {
        if (!held) return;
#ifdef _WIN32
        CloseHandle(handle);
#else
        if (folder) fs::permissions(path.parent_path(), fs::perms::owner_all, fs::perm_options::replace);
        else fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
#endif
        held = false;
    }
    ~FileHold() { Release(); }
};

struct Asker {
    int move = 0, newer = 0;
    std::optional<bool> answer = true;
    pt::platform::LegacyAsk Fn() {
        return [this](LegacyQuestion question, const fs::path&) {
            (question == LegacyQuestion::Move ? move : newer)++;
            return answer;
        };
    }
};
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = fs::absolute(argv[1]) / fs::path(u8"ユーザーデータ-éğ-Ж");
    if (fs::exists(root)) return 2;
    fs::create_directories(root);
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
        failures += !ok;
    };

    {
        Machine m{root / "fresh"};
        Write(m.Legacy() / "pt.ini", "legacy settings");
        Write(m.Legacy() / "PT_Save_Data0", "legacy save 0");
        Write(m.Legacy() / "PT_Save_Data1", "legacy save 1");
        Write(m.Legacy() / "pt.log", "legacy log");
        Write(m.Legacy() / "cache" / "previews" / "thumb.bin", "preview");
        for (const char* name : {"pt.ini", "PT_Save_Data0", "PT_Save_Data1", "pt.log"}) Age(m.Legacy() / name, 48);
        const auto saveTime = fs::last_write_time(m.Legacy() / "PT_Save_Data1");
        Write(m.Install(1) / "pt.log", "this run's log");
        Asker ask;
        const auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        const auto dest = m.Install(1);
        check(report.success && report.migrated_legacy && !report.legacy_failed && ask.move == 1 && ask.newer == 0,
              "fresh move: Yes asked once and succeeded");
        check(Read(dest / "pt.ini") == "legacy settings" && Read(dest / "PT_Save_Data0") == "legacy save 0" &&
                  Read(dest / "PT_Save_Data1") == "legacy save 1" && Read(dest / "cache" / "previews" / "thumb.bin") == "preview" &&
                  Read(dest / "pt.log") == "this run's log" && Read(dest / "pt.log.bak") == "legacy log",
              "fresh move: contents arrive, the newer pt.log stays and the old one is pt.log.bak");
        check(fs::last_write_time(dest / "PT_Save_Data1") == saveTime, "fresh move: copies keep the original write time");
        check(!fs::exists(m.Legacy()) && report.removed_legacy && report.files_removed == 5, "fresh move: originals and old folders are gone");
        const auto moved = Read(m.PtPort() / "MOVED_TO.txt");
        check(OnlyMarkerLeft(m.PtPort(), "MOVED_TO.txt") && moved.find(U8(dest)) != std::string::npos &&
                  moved.find("Moved to: ") == 0 && moved.find("Date: ") != std::string::npos,
              "fresh move: only MOVED_TO.txt (destination and date) is left in the old pt-port folder");
        check(fs::exists(dest / ".pt-user-data-migration-v1"), "fresh move: destination marker written");

        Asker second;
        auto again = PrepareUserDataDirectory(m.Install(2), m.Legacy(), true, second.Fn());
        Write(m.Legacy() / "PT_Save_Data0", "1.0.1 played again");
        auto third = PrepareUserDataDirectory(m.Install(3), m.Legacy(), true, second.Fn());
        check(again.success && third.success && second.move == 0 && second.newer == 0 && !fs::exists(m.Install(3) / "PT_Save_Data0") &&
                  Read(m.Legacy() / "PT_Save_Data0") == "1.0.1 played again",
              "second install after a move: no question, nothing touched");
    }

    {
        Machine m{root / "no"};
        Write(m.Legacy() / "PT_Save_Data0", "old save");
        Write(m.Legacy() / "pt.ini", "old settings");
        Asker ask;
        ask.answer = false;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        check(report.success && report.declined_legacy && ask.move == 1 && Read(m.Legacy() / "PT_Save_Data0") == "old save" &&
                  CountFiles(m.Legacy()) == 2 && !fs::exists(m.Install(1) / "PT_Save_Data0"),
              "No keeps the old folder untouched");
        check(fs::exists(m.PtPort() / "NOT_MOVED.txt") && Read(m.PtPort() / "NOT_MOVED.txt").find("Date: ") != std::string::npos,
              "No writes NOT_MOVED.txt in the old location");
        report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        auto other = PrepareUserDataDirectory(m.Install(2), m.Legacy(), true, ask.Fn());
        check(report.success && other.success && ask.move == 1 && ask.newer == 0 && !fs::exists(m.Install(2) / "PT_Save_Data0") &&
                  CountFiles(m.Legacy()) == 2,
              "No is not asked again by this or a second install folder");
    }

    {
        Machine m{root / "pending"};
        Write(m.Legacy() / "PT_Save_Data0", "old save");
        Asker ask;
        ask.answer = std::nullopt;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        check(report.success && report.legacy_pending && !fs::exists(m.Install(1) / ".pt-user-data-migration-v1") &&
                  !fs::exists(m.PtPort() / "MOVED_TO.txt") && !fs::exists(m.PtPort() / "NOT_MOVED.txt") &&
                  Read(m.Legacy() / "PT_Save_Data0") == "old save",
              "an unshown question leaves everything for the next start");
        ask.answer = true;
        report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        check(report.migrated_legacy && ask.move == 2 && !fs::exists(m.Legacy()), "the next start asks and moves");
    }

    {
        Machine m{root / "fail"};
        Write(m.Legacy() / "PT_Save_Data0", "save zero");
        Write(m.Legacy() / "PT_Save_Data1", "save one");
        Write(m.Legacy() / "pt.ini", "settings");
        Write(m.Legacy() / "zz" / "late.bin", "late file");
        const fs::path victim = m.Legacy() / "pt.ini";
        bool locked = false;
        int staged = 0;
#ifdef _WIN32
        HANDLE lock = INVALID_HANDLE_VALUE;
#endif
        pt::platform::UserDataHooks hooks;
        hooks.after_stage = [&](const fs::path&) {
            if (++staged != 1) return;
#ifdef _WIN32
            lock = CreateFileW(victim.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            locked = lock != INVALID_HANDLE_VALUE;
#else
            locked = ::geteuid() != 0;
            if (locked) fs::permissions(victim, fs::perms::none, fs::perm_options::replace);
#endif
        };
        Asker ask;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn(), hooks);
#ifdef _WIN32
        if (locked) CloseHandle(lock);
#else
        if (locked) fs::permissions(victim, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
#endif
        if (!locked) {
            check(true, "copy failure fixture unavailable (privileged user)");
        } else {
            for (const auto& error : report.legacy_errors) std::printf("  reported: %s\n", error.c_str());
            check(report.success && report.legacy_failed && !report.legacy_errors.empty() && !report.migrated_legacy && staged >= 2,
                  "failure mid-copy is reported after earlier files were copied");
            check(Read(m.Legacy() / "PT_Save_Data0") == "save zero" && Read(m.Legacy() / "PT_Save_Data1") == "save one" &&
                      Read(m.Legacy() / "zz" / "late.bin") == "late file" && Read(victim) == "settings",
                  "failure mid-copy keeps every original");
            check(CountFiles(m.Install(1)) == 0 && !fs::exists(m.PtPort() / "MOVED_TO.txt") &&
                      !fs::exists(m.Install(1) / ".pt-user-data-migration-v1"),
                  "failure mid-copy leaves no partial copy and no marker");
            report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
            check(report.migrated_legacy && ask.move == 2 && Read(m.Install(1) / "pt.ini") == "settings" &&
                      Read(m.Install(1) / "zz" / "late.bin") == "late file" && !fs::exists(m.Legacy()),
                  "after a failure the next start asks again and moves");
        }
    }

    {
        Machine m{root / "verify"};
        Write(m.Legacy() / "PT_Save_Data0", "the real save");
        Write(m.Legacy() / "pt.ini", "settings");
        Asker ask;
        pt::platform::UserDataHooks hooks;
        hooks.after_stage = [](const fs::path& staged) {
            std::ofstream out(staged, std::ios::binary | std::ios::app);
            out << "x";
        };
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn(), hooks);
        check(report.legacy_failed && Read(m.Legacy() / "PT_Save_Data0") == "the real save" && Read(m.Legacy() / "pt.ini") == "settings" &&
                  CountFiles(m.Install(1)) == 0,
              "a copy that fails its check deletes nothing and publishes nothing");
    }

    {
        Machine m{root / "dest-newer"};
        Write(m.Legacy() / "PT_Save_Data0", "old progress 0");
        Write(m.Legacy() / "PT_Save_Data1", "old progress 1");
        Age(m.Legacy() / "PT_Save_Data0", 72);
        Age(m.Legacy() / "PT_Save_Data1", 70);
        Write(m.Install(1) / "PT_Save_Data0", "new progress 0");
        Write(m.Install(1) / "PT_Save_Data1", "new progress 1");
        Asker ask;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        const auto dest = m.Install(1);
        check(report.migrated_legacy && Read(dest / "PT_Save_Data0") == "new progress 0" && Read(dest / "PT_Save_Data1") == "new progress 1",
              "newer destination saves are not overwritten");
        check(Read(dest / "PT_Save_Data0.bak") == "old progress 0" && Read(dest / "PT_Save_Data1.bak") == "old progress 1" &&
                  !fs::exists(m.Legacy()) && fs::exists(m.PtPort() / "MOVED_TO.txt"),
              "the older old saves are kept as .bak and the old folder is moved");
    }
    {
        Machine m{root / "old-newer"};
        Write(m.Legacy() / "PT_Save_Data0", "newest progress");
        Write(m.Install(1) / "PT_Save_Data0", "stale 0");
        Write(m.Install(1) / "PT_Save_Data1", "stale 1");
        Age(m.Install(1) / "PT_Save_Data0", 24);
        Age(m.Install(1) / "PT_Save_Data1", 25);
        Asker ask;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        const auto dest = m.Install(1);
        check(report.migrated_legacy && Read(dest / "PT_Save_Data0") == "newest progress" && !fs::exists(dest / "PT_Save_Data1") &&
                  Read(dest / "PT_Save_Data0.bak") == "stale 0" && Read(dest / "PT_Save_Data1.bak") == "stale 1",
              "newer old saves replace the destination set, which is kept as .bak");
    }

    {
        Machine m{root / "copied-same"};
        Write(m.Legacy() / "PT_Save_Data0", "save");
        Write(m.Legacy() / "PT_Save_Data1", "save older slot");
        Write(m.Legacy() / "pt.ini", "settings then");
        Write(m.Legacy() / "pt.log", "old log");
        for (const char* name : {"PT_Save_Data0", "PT_Save_Data1", "pt.ini", "pt.log"}) Age(m.Legacy() / name, 100);
        const auto dest = m.Install(1);
        Write(dest / ".pt-user-data-migration-v1", "1\n");
        Write(dest / "PT_Save_Data0", "save");
        Write(dest / "PT_Save_Data1", "save older slot");
        Write(dest / "pt.ini", "settings changed since");
        Write(dest / "pt.log", "this run");
        Asker ask;
        auto report = PrepareUserDataDirectory(dest, m.Legacy(), true, ask.Fn());
        check(report.success && report.cleaned_legacy && ask.move == 0 && ask.newer == 0, "identical old copy: cleaned up without a question");
        check(!fs::exists(m.Legacy()) && OnlyMarkerLeft(m.PtPort(), "MOVED_TO.txt") && Read(dest / "PT_Save_Data0") == "save" &&
                  Read(dest / "pt.ini") == "settings changed since" && Read(dest / "pt.ini.bak") == "settings then",
              "identical old copy: old folder removed, older settings kept as .bak");
        Asker later;
        auto again = PrepareUserDataDirectory(m.Install(2), m.Legacy(), true, later.Fn());
        check(again.success && later.move == 0 && later.newer == 0, "identical old copy: a second install folder is not asked");
    }
    {
        Machine m{root / "copied-older"};
        Write(m.Legacy() / "PT_Save_Data0", "progress at copy time");
        Age(m.Legacy() / "PT_Save_Data0", 200);
        const auto dest = m.Install(1);
        Write(dest / ".pt-user-data-migration-v1", "1\n");
        Write(dest / "PT_Save_Data0", "progress played on");
        Asker ask;
        auto report = PrepareUserDataDirectory(dest, m.Legacy(), true, ask.Fn());
        check(report.cleaned_legacy && ask.newer == 0 && Read(dest / "PT_Save_Data0") == "progress played on" &&
                  Read(dest / "PT_Save_Data0.bak") == "progress at copy time" && !fs::exists(m.Legacy()),
              "older old copy: cleaned up without a question, kept as .bak");
    }
    {
        Machine m{root / "copied-newer"};
        const auto dest = m.Install(1);
        Write(dest / ".pt-user-data-migration-v1", "1\n");
        Write(dest / "PT_Save_Data0", "copied long ago");
        Age(dest / "PT_Save_Data0", 300);
        Write(m.Legacy() / "PT_Save_Data0", "played 1.0.1 again");
        Asker ask;
        ask.answer = false;
        auto report = PrepareUserDataDirectory(dest, m.Legacy(), true, ask.Fn());
        check(report.declined_legacy && ask.newer == 1 && ask.move == 0 && Read(m.Legacy() / "PT_Save_Data0") == "played 1.0.1 again" &&
                  Read(dest / "PT_Save_Data0") == "copied long ago" && fs::exists(m.PtPort() / "NOT_MOVED.txt"),
              "newer old copy: asked once; No keeps both");
        report = PrepareUserDataDirectory(dest, m.Legacy(), true, ask.Fn());
        check(ask.newer == 1 && Read(m.Legacy() / "PT_Save_Data0") == "played 1.0.1 again", "newer old copy: No is not asked again");
    }
    {
        Machine m{root / "copied-newer-yes"};
        const auto dest = m.Install(1);
        Write(dest / ".pt-user-data-migration-v1", "1\n");
        Write(dest / "PT_Save_Data0", "copied long ago");
        Age(dest / "PT_Save_Data0", 300);
        Write(m.Legacy() / "PT_Save_Data0", "played 1.0.1 again");
        Asker ask;
        auto report = PrepareUserDataDirectory(dest, m.Legacy(), true, ask.Fn());
        check(report.migrated_legacy && ask.newer == 1 && Read(dest / "PT_Save_Data0") == "played 1.0.1 again" &&
                  Read(dest / "PT_Save_Data0.bak") == "copied long ago" && !fs::exists(m.Legacy()) &&
                  fs::exists(m.PtPort() / "MOVED_TO.txt"),
              "newer old copy: Yes moves it and keeps the replaced save as .bak");
    }
    {
        Machine m{root / "copied-reset"};
        const auto dest = m.Install(1);
        Write(dest / ".pt-user-data-migration-v1", "1\n");
        Write(m.Legacy() / "PT_Save_Data0", "before the reset");
        Asker ask;
        ask.answer = false;
        auto report = PrepareUserDataDirectory(dest, m.Legacy(), true, ask.Fn());
        check(ask.newer == 1 && report.declined_legacy && !fs::exists(dest / "PT_Save_Data0") &&
                  Read(m.Legacy() / "PT_Save_Data0") == "before the reset",
              "old copy with no saves in the destination: asked, not restored silently");
    }

    {
        Machine m{root / "native"};
        pt::AppSettings oldSettings;
        oldSettings.audio.volume = 0.42f;
        oldSettings.voice.key = "J";
        check(pt::SaveAppSettings(m.Legacy() / "pt.ini", oldSettings), "legacy settings fixture created");
        pt::game::SaveStore oldStore;
        oldStore.SetDirectory(m.Legacy(), "PT_Save_Data");
        pt::game::SaveFile oldSave;
        oldSave.progress.floor = "f050";
        check(oldStore.Save(oldSave), "legacy save fixture created");
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), false);
        check(report.success && !report.migrated_legacy && !fs::exists(m.Install(1) / ".pt-user-data-migration-v1") &&
                  !fs::exists(m.Install(1) / "pt.ini"),
              "headless preparation leaves migration pending and writes no marker");
        Asker ask;
        report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        pt::AppSettings loadedSettings;
        pt::game::SaveStore newStore;
        newStore.SetDirectory(m.Install(1), "PT_Save_Data");
        const auto loadedSave = newStore.Load();
        check(report.success && report.migrated_legacy && pt::LoadAppSettings(m.Install(1) / "pt.ini", loadedSettings) &&
                  loadedSettings.voice.key == "J" && loadedSettings.audio.volume == oldSettings.audio.volume && loadedSave &&
                  loadedSave->progress.floor == "f050",
              "moved settings and save files restore through native loaders");
    }

    {
        Machine m{root / "blocked-entry"};
        Write(m.Legacy() / "nested" / "save", "retry save");
        fs::create_directories(m.Install(1) / "nested" / "save");
        Write(m.Install(1) / "nested" / "save" / "blocker", "destination conflict");
        Asker ask;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        const bool failedSafely = report.success && report.legacy_failed && Read(m.Legacy() / "nested" / "save") == "retry save" &&
                                  !fs::exists(m.Install(1) / ".pt-user-data-migration-v1");
        fs::remove_all(m.Install(1) / "nested" / "save");
        report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        check(failedSafely && report.migrated_legacy && Read(m.Install(1) / "nested" / "save") == "retry save" && ask.move == 2,
              "a blocked move keeps the original and retries at the next start");
    }

    {
        Machine m{root / "invalid-marker"};
        Write(m.Legacy() / "PT_Save_Data0", "must not import under invalid marker");
        fs::create_directories(m.Install(1) / ".pt-user-data-migration-v1");
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, Asker().Fn());
        check(!report.success && !report.errors.empty() && !fs::exists(m.Install(1) / "PT_Save_Data0") &&
                  fs::exists(m.Legacy() / "PT_Save_Data0"),
              "directory at marker path cannot suppress migration");
    }

    {
        const auto sameDest = root / "same-path" / "data";
        Write(sameDest / "PT_Save_Data0", "already in destination");
        Asker ask;
        auto report = PrepareUserDataDirectory(sameDest, sameDest, true, ask.Fn());
        check(report.success && !report.migrated_legacy && ask.move == 0 && Read(sameDest / "PT_Save_Data0") == "already in destination",
              "identical legacy and destination paths do not self-migrate (the macOS bundle case)");
    }

    {
        const auto blocked = root / "blocked";
        Write(blocked, "not a directory");
        auto report = PrepareUserDataDirectory(blocked, root / "nothing", true, Asker().Fn());
        check(!report.success && !report.errors.empty(), "destination write probe failure is reported");
    }
#ifndef _WIN32
    {
        const auto readOnly = root / "read-only";
        fs::create_directories(readOnly);
        if (::geteuid() == 0) {
            check(true, "read-only permission check skipped for privileged test user");
        } else {
            fs::permissions(readOnly, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
            auto report = PrepareUserDataDirectory(readOnly, root / "nothing");
            fs::permissions(readOnly, fs::perms::owner_all, fs::perm_options::replace);
            check(!report.success && !report.errors.empty(), "read-only directory fails real write probe");
        }
    }
#endif

    {
        Machine m{root / "links"};
        Write(root / "outside" / "secret", "outside");
        Write(m.Legacy() / "PT_Save_Data0", "save");
        std::error_code linkError;
        fs::create_directory_symlink(root / "outside", m.Legacy() / "linked", linkError);
        if (linkError) {
            check(true, "symlink fixture unavailable on this platform");
        } else {
            auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, Asker().Fn());
            check(report.migrated_legacy && !fs::exists(m.Install(1) / "linked" / "secret") && !report.warnings.empty() &&
                      Read(root / "outside" / "secret") == "outside" && Read(m.Install(1) / "PT_Save_Data0") == "save",
                  "legacy symlink directory is skipped and its target untouched");
        }
    }

    {
        Machine m{root / "caches"};
        Write(m.Legacy() / "PT_Save_Data0", "save");
        Write(m.Legacy() / "pt.ini", "settings");
        Write(m.Legacy() / "enhanced-textures" / "156b795ba9d00671.pttex", "upscaled");
        Write(m.Legacy() / "enhanced-textures" / "upscaler.log", "log");
        Write(m.Legacy() / "enhanced-textures-2048" / "a.pttex", "capped");
        Write(m.Legacy() / "pipeline-cache" / "pipelines.bin", "pipelines");
        Write(m.Legacy() / "loop-previews-v3" / "loop1.png", "preview");
        Write(m.Legacy() / "museum-previews-v2" / "item.png", "thumb");
        Asker ask;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        const auto dest = m.Install(1);
        check(report.migrated_legacy && !report.legacy_failed && report.legacy_errors.empty() && report.files_removed == 2 &&
                  report.files_copied == 2 && Read(dest / "PT_Save_Data0") == "save" && Read(dest / "pt.ini") == "settings",
              "caches: only the saves and settings are moved and verified");
        check(!fs::exists(dest / "enhanced-textures") && !fs::exists(dest / "pipeline-cache") && !fs::exists(dest / "loop-previews-v3") &&
                  !fs::exists(dest / "museum-previews-v2") && !fs::exists(dest / "enhanced-textures-2048"),
              "caches: no cache is copied into the data folder");
        check(!fs::exists(m.Legacy()) && OnlyMarkerLeft(m.PtPort(), "MOVED_TO.txt"), "caches: deleted afterwards with the old folder");
    }

    {
        Machine m{root / "cache-locked"};
        Write(m.Legacy() / "PT_Save_Data0", "save");
        Write(m.Legacy() / "enhanced-textures" / "free.pttex", "free");
        const fs::path busy = m.Legacy() / "enhanced-textures" / "held" / "locked.pttex";
        Write(busy, "in use");
        FileHold hold(busy, true);
        Asker ask;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        const bool held = hold.held, stayed = fs::exists(busy);
        hold.Release();
        if (!held) {
            check(true, "locked cache fixture unavailable (privileged user)");
        } else {
            check(stayed && report.migrated_legacy && !report.legacy_failed && report.legacy_errors.empty() &&
                      Read(m.Install(1) / "PT_Save_Data0") == "save" && !fs::exists(m.Legacy() / "PT_Save_Data0") &&
                      !fs::exists(m.Legacy() / "enhanced-textures" / "free.pttex") && fs::exists(m.PtPort() / "MOVED_TO.txt"),
                  "locked cache: saves moved, MOVED_TO written, no error");
            Asker later;
            auto again = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, later.Fn());
            check(again.success && !again.legacy_failed && later.move == 0 && later.newer == 0 && !fs::exists(m.Legacy()) &&
                      OnlyMarkerLeft(m.PtPort(), "MOVED_TO.txt"),
                  "locked cache: a later start deletes the leftover cache without a question");
        }
    }

    {
        Machine m{root / "cache-busy"};
        Write(m.Legacy() / "PT_Save_Data0", "save");
        Write(m.Legacy() / "PT_Save_Data1", "save 1");
        Write(m.Legacy() / "pt.ini", "settings");
        const fs::path busy = m.Legacy() / "enhanced-textures" / "156b79749f9e42d6.pttex";
        Write(busy, "start");
        std::atomic<bool> stop{false};
        std::atomic<int> writes{0};
        std::thread writer([&] {
            for (int n = 0; !stop; ++n) {
                std::ofstream out(busy, std::ios::binary | std::ios::trunc);
                if (out) out << std::string(4096 + n % 512, char('a' + n % 26));
                ++writes;
            }
        });
        while (writes < 20) std::this_thread::yield();
        Asker ask;
        pt::platform::UserDataHooks hooks;
        int before = 0;
        hooks.after_stage = [&](const fs::path&) {
            before = writes;
            while (writes < before + 5) std::this_thread::yield();
        };
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn(), hooks);
        stop = true;
        writer.join();
        for (const auto& error : report.legacy_errors) std::printf("  reported: %s\n", error.c_str());
        check(report.migrated_legacy && !report.legacy_failed && report.legacy_errors.empty() &&
                  Read(m.Install(1) / "PT_Save_Data1") == "save 1" && Read(m.Install(1) / "pt.ini") == "settings" &&
                  !fs::exists(m.Legacy() / "PT_Save_Data0") && fs::exists(m.PtPort() / "MOVED_TO.txt") &&
                  !fs::exists(m.Install(1) / "enhanced-textures"),
              "cache written during the move: saves moved, no error, cache not copied");
        Asker later;
        auto again = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, later.Fn());
        check(!again.legacy_failed && later.move == 0 && !fs::exists(m.Legacy()), "cache written during the move: a later start finishes");
    }

    {
        Machine m{root / "other-locked"};
        Write(m.Legacy() / "PT_Save_Data0", "save");
        Write(m.Legacy() / "pt.ini", "settings");
        Write(m.Legacy() / "pt.log", "the old version's log");
        Age(m.Legacy() / "pt.log", 10);
        Write(m.Install(1) / "pt.log", "this run's log");
        FileHold hold(m.Legacy() / "pt.log", false);
        Asker ask;
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, ask.Fn());
        const bool held = hold.held;
        hold.Release();
        if (!held) {
            check(true, "other file in use fixture unavailable (privileged user)");
        } else {
            check(report.migrated_legacy && !report.legacy_failed && report.legacy_errors.empty() && !report.warnings.empty() &&
                      Read(m.Install(1) / "PT_Save_Data0") == "save" && !fs::exists(m.Legacy() / "PT_Save_Data0") &&
                      Read(m.Legacy() / "pt.log") == "the old version's log" && !fs::exists(m.PtPort() / "MOVED_TO.txt"),
                  "other old file in use: saves moved, the file stays with a warning, no error");
            Asker later;
            auto again = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, later.Fn());
            check(again.cleaned_legacy && !again.legacy_failed && later.move == 0 && later.newer == 0 && !fs::exists(m.Legacy()) &&
                      Read(m.Install(1) / "pt.log.bak") == "the old version's log" && OnlyMarkerLeft(m.PtPort(), "MOVED_TO.txt"),
                  "other old file in use: a later start cleans it up silently");
        }
    }

    {
        Machine m{root / "copied-locked"};
        Write(m.Legacy() / "PT_Save_Data0", "save");
        Age(m.Legacy() / "PT_Save_Data0", 100);
        const auto dest = m.Install(1);
        Write(dest / ".pt-user-data-migration-v1", "1\n");
        Write(dest / "PT_Save_Data0", "save");
        FileHold hold(m.Legacy() / "PT_Save_Data0", false);
        Asker ask;
        auto report = PrepareUserDataDirectory(dest, m.Legacy(), true, ask.Fn());
        const bool held = hold.held;
        hold.Release();
        if (!held) {
            check(true, "silent cleanup failure fixture unavailable (privileged user)");
        } else {
            check(report.success && report.legacy_failed && !report.asked && ask.move == 0 && ask.newer == 0 &&
                      Read(m.Legacy() / "PT_Save_Data0") == "save" && !fs::exists(m.PtPort() / "MOVED_TO.txt"),
                  "silent cleanup: an unreadable save stays, no question (logged only)");
            report = PrepareUserDataDirectory(dest, m.Legacy(), true, ask.Fn());
            check(report.cleaned_legacy && !report.legacy_failed && !fs::exists(m.Legacy()), "silent cleanup: a later start finishes");
        }
    }

    {
        Machine m{root / "two-at-once"};
        Write(m.Legacy() / "PT_Save_Data0", "save");
        Write(m.Legacy() / "pt.ini", "settings");
        Write(m.Legacy() / "enhanced-textures" / "x.pttex", "cache");
        Asker first, busy, after;
        pt::platform::UserDataReport busyReport, afterReport;
        std::thread late;
        pt::platform::UserDataHooks hooks;
        hooks.after_lock = [&] {
            std::thread blocked([&] {
                pt::platform::UserDataHooks quick;
                quick.lock_wait = std::chrono::milliseconds(200);
                busyReport = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, busy.Fn(), quick);
            });
            blocked.join();
            late = std::thread([&] {
                pt::platform::UserDataHooks patient;
                patient.lock_wait = std::chrono::milliseconds(20000);
                afterReport = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, after.Fn(), patient);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        };
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, first.Fn(), hooks);
        late.join();
        check(report.migrated_legacy && first.move == 1 && !fs::exists(m.Legacy()) && OnlyMarkerLeft(m.PtPort(), "MOVED_TO.txt"),
              "two at once: the lock holder moves the old folder");
        check(busyReport.success && busyReport.legacy_busy && busy.move == 0 && busy.newer == 0 && !busyReport.legacy_failed &&
                  busyReport.legacy_errors.empty() && busyReport.warnings.empty() && !busyReport.migrated_legacy &&
                  !busyReport.cleaned_legacy,
              "two at once: the second copy skips the old folder cleanly while the lock is held");
        check(afterReport.success && !afterReport.legacy_busy && after.move == 0 && after.newer == 0 && !afterReport.legacy_failed &&
                  afterReport.legacy_errors.empty() && !afterReport.migrated_legacy && !afterReport.cleaned_legacy,
              "two at once: a copy that gets the lock afterwards finds the folder moved and does nothing");
    }
    {
        Machine m{root / "busy-no-marker"};
        Write(m.Legacy() / "PT_Save_Data0", "save");
        Asker first, second;
        first.answer = std::nullopt;
        pt::platform::UserDataReport secondReport;
        pt::platform::UserDataHooks hooks;
        hooks.after_lock = [&] {
            std::thread other([&] {
                pt::platform::UserDataHooks quick;
                quick.lock_wait = std::chrono::milliseconds(100);
                secondReport = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, second.Fn(), quick);
            });
            other.join();
        };
        auto report = PrepareUserDataDirectory(m.Install(1), m.Legacy(), true, first.Fn(), hooks);
        check(report.legacy_pending && secondReport.legacy_busy && second.move == 0 &&
                  !fs::exists(m.Install(1) / ".pt-user-data-migration-v1") && Read(m.Legacy() / "PT_Save_Data0") == "save",
              "busy lock: no marker, nothing decided");
    }

    fs::remove_all(root);
    std::printf("user data: %d failures\n", failures);
    return failures ? 1 : 0;
}
