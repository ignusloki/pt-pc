#include "game/first_run.h"

#include <SDL3/SDL.h>

#include "engine/core/log.h"
#include "engine/platform/os.h"
#include "game/save_data.h"
#include "game/ui/pc_settings.h"
#include "setup_core.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <thread>

namespace pt::first_run {
namespace fs = std::filesystem;

namespace {

constexpr int kLanguages = 14;

std::string Utf8(const fs::path& path) { return pt::os::PathToUtf8(path); }

enum class Kind { Package, Folder, Quit, Cancel, NoDialog };
struct Choice {
    Kind kind = Kind::Quit;
    fs::path path;
};

std::optional<std::deque<Choice>> TestScript() {
    const std::string text = pt::os::GetEnv("PT_TEST_FIRST_RUN");
    if (text.empty()) return std::nullopt;
    std::deque<Choice> script;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('|', start);
        if (end == std::string::npos) end = text.size();
        const std::string entry = text.substr(start, end - start);
        if (entry.starts_with("pkg=")) {
            script.push_back({Kind::Package, pt::os::PathFromUtf8(entry.substr(4))});
        } else if (entry.starts_with("folder=")) {
            script.push_back({Kind::Folder, pt::os::PathFromUtf8(entry.substr(7))});
        } else if (entry == "cancel") {
            script.push_back({Kind::Cancel, {}});
        } else if (entry == "nodialog") {
            script.push_back({Kind::NoDialog, {}});
        } else if (entry == "quit") {
            script.push_back({Kind::Quit, {}});
        } else if (!entry.empty()) {
            pt::LogWarn("first run: PT_TEST_FIRST_RUN entry '{}' is not pkg=, folder=, cancel, nodialog or quit", entry);
        }
        start = end + 1;
    }
    return script;
}

uint64_t FileSize(const fs::path& file) {
    std::error_code error;
    const auto size = fs::file_size(file, error);
    return error ? 0 : static_cast<uint64_t>(size);
}

uint64_t OutputBytes(const fs::path& dir) {
    uint64_t sum = 0;
    std::error_code error;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error)) {
        std::error_code size_error;
        const auto size = fs::file_size(it->path(), size_error);
        if (!size_error) sum += static_cast<uint64_t>(size);
    }
    return sum;
}

std::string ReadTail(const fs::path& file, size_t limit) {
    std::ifstream input(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), {});
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text.size() > limit ? text.substr(text.size() - limit) : text;
}

class Ui {
public:
    explicit Ui(const Context& context) : context_(context) {
        if (!context_.interactive) return;
#ifdef __APPLE__
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
#endif
        video_ = SDL_InitSubSystem(SDL_INIT_VIDEO);
        if (!video_) pt::LogWarn("first run: no video subsystem ({}); message boxes only", SDL_GetError());
    }
    ~Ui() {
        if (video_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
#ifdef __APPLE__
        if (context_.interactive) SDL_ResetHint(SDL_HINT_MAC_BACKGROUND_APP);
#endif
    }

    Kind Ask() {
        const std::string title = T("pc_first_run_title"), text = T("pc_first_run_prompt");
        const std::string pkg = T("pc_first_run_choose_pkg"), folder = T("pc_first_run_choose_folder"), quit = T("pc_first_run_quit");
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, pkg.c_str()},
            {0, 2, folder.c_str()},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 3, quit.c_str()},
        };
        const SDL_MessageBoxData data{SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, nullptr, title.c_str(), text.c_str(),
                                      SDL_arraysize(buttons), buttons, nullptr};
        int pressed = -1;
        if (!SDL_ShowMessageBox(&data, &pressed)) {
            pt::LogError("first run: the screen could not be shown: {}", SDL_GetError());
            std::fprintf(stderr, "%s\n\n%s\n", text.c_str(), Layout().c_str());
            return Kind::Quit;
        }
        return pressed == 1 ? Kind::Package : pressed == 2 ? Kind::Folder : Kind::Quit;
    }

    void Message(const std::string& text, bool last = false) {
        pt::LogInfo("first run: message:\n{}", text);
        if (!context_.interactive) return;
        const std::string title = T("pc_first_run_title"), label = T(last ? "pc_first_run_quit" : "pc_first_run_back");
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, label.c_str()}};
        const SDL_MessageBoxData data{last ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_WARNING, nullptr, title.c_str(), text.c_str(), 1, buttons, nullptr};
        int pressed = 0;
        if (!SDL_ShowMessageBox(&data, &pressed)) std::fprintf(stderr, "%s\n", text.c_str());
    }

    Kind Pick(bool folder, fs::path& picked) {
        struct Wait {
            std::atomic<bool> done{false};
            bool failed = false;
            std::string path;
            std::string error;
        } wait;
        const std::string title = T(folder ? "pc_first_run_pick_folder" : "pc_first_run_pick_pkg");
        const std::string pkg_name = T("pc_first_run_pkg_filter") + " (*.pkg)", all_name = T("pc_first_run_all_files");
        const SDL_DialogFileFilter filters[] = {{pkg_name.c_str(), "pkg"}, {all_name.c_str(), "*"}};
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
        std::string location;
        if (!folder) {
            SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
            SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, SDL_arraysize(filters));
            if (const char* downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)) location = downloads;
            if (!location.empty() && location.back() != '/' && location.back() != '\\') location += '/';
            if (!location.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
        }
        SDL_ShowFileDialogWithProperties(
            folder ? SDL_FILEDIALOG_OPENFOLDER : SDL_FILEDIALOG_OPENFILE,
            [](void* user, const char* const* list, int) {
                auto* w = static_cast<Wait*>(user);
                if (!list) {
                    w->failed = true;
                    w->error = SDL_GetError();
                } else if (list[0]) {
                    w->path = list[0];
                }
                w->done = true;
            },
            &wait, props);
        SDL_DestroyProperties(props);
        while (!wait.done) {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
        if (wait.failed) {
            pt::LogWarn("first run: the {} window could not be opened: {}", folder ? "folder" : "file", wait.error);
            return Kind::NoDialog;
        }
        if (wait.path.empty()) return Kind::Cancel;
        picked = pt::os::PathFromUtf8(wait.path);
        return folder ? Kind::Folder : Kind::Package;
    }

    bool Progress(double fraction) {
        const int percent = std::clamp(static_cast<int>(fraction * 100.0), 0, 99);
        if (!context_.interactive) {
            if (percent / 10 != logged_ / 10) pt::LogInfo("first run: extracting, {}%", percent);
            logged_ = percent;
            return true;
        }
        if (!video_) return true;
        if (!window_) {
            window_ = SDL_CreateWindow(T("pc_first_run_extracting", {"0"}).c_str(), 640, 120, SDL_WINDOW_HIDDEN);
            if (window_) renderer_ = SDL_CreateRenderer(window_, nullptr);
            if (!window_ || !renderer_) {
                pt::LogWarn("first run: no progress window ({})", SDL_GetError());
                CloseWindow();
                video_ = false;
                return true;
            }
            SDL_ShowWindow(window_);
            SDL_RaiseWindow(window_);
        }
        bool open = true;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) open = false;
        }
        if (percent != shown_) {
            shown_ = percent;
            SDL_SetWindowTitle(window_, T("pc_first_run_extracting", {std::to_string(percent)}).c_str());
        }
        int w = 640, h = 120;
        SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
        SDL_SetRenderDrawColor(renderer_, 16, 16, 16, 255);
        SDL_RenderClear(renderer_);
        const float margin = w * 0.06f, bar_h = h * 0.22f, top = h * 0.25f;
        const SDL_FRect frame{margin, top, w - 2 * margin, bar_h};
        SDL_SetRenderDrawColor(renderer_, 110, 110, 110, 255);
        SDL_RenderRect(renderer_, &frame);
        const SDL_FRect fill{frame.x + 3, frame.y + 3, (frame.w - 6) * static_cast<float>(std::clamp(fraction, 0.0, 1.0)), frame.h - 6};
        SDL_SetRenderDrawColor(renderer_, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer_, &fill);
        const std::string number = std::to_string(percent) + "%";
        const float scale = std::max(1.0f, h / 60.0f);
        SDL_SetRenderScale(renderer_, scale, scale);
        SDL_RenderDebugText(renderer_, (w / scale - 8.0f * number.size()) / 2.0f, (top + bar_h + h * 0.12f) / scale, number.c_str());
        SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
        SDL_RenderPresent(renderer_);
        return open;
    }
    void CloseWindow() {
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        renderer_ = nullptr;
        window_ = nullptr;
        shown_ = -1;
        logged_ = -100;
    }

    std::string T(std::string_view key, std::initializer_list<std::string> args = {}) const { return Text(key, context_.language, args); }
    std::string Layout() const {
        return T("pc_first_run_layout", {Utf8(context_.program_dir_read_only ? context_.user_dir : context_.program_dir)});
    }

private:
    const Context& context_;
    bool video_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    int shown_ = -1, logged_ = -100;
};

struct Outcome {
    fs::path game_dir;
    std::string error;
    bool extracted = false;
};

class Flow {
public:
    Flow(const Context& context, Ui& ui) : c_(context), ui_(ui) {}

    Outcome UseFolder(const fs::path& picked) {
        pt::LogInfo("first run: folder {}", Utf8(picked));
        const fs::path game = FindGameFolder(picked);
        if (game.empty()) {
            std::vector<fs::path> packages;
            std::error_code error;
            for (fs::directory_iterator it(picked, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error)) {
                if (it->is_regular_file(error) && pt::setup::IsPackage(it->path())) packages.push_back(it->path());
            }
            if (packages.size() == 1) {
                pt::LogInfo("first run: no game files in the folder, but one PKG: {}", Utf8(packages[0]));
                return UsePackage(packages[0]);
            }
            if (packages.size() > 1) return {{}, ui_.T("pc_first_run_several_pkgs", {Utf8(picked)})};
            return {{}, ui_.T("pc_first_run_not_found", {Utf8(picked)})};
        }
        if (const std::string error = Check(game); !error.empty()) return {{}, error};
        return {game};
    }

    Outcome UsePackageFile(const fs::path& file) {
        pt::LogInfo("first run: file {}", Utf8(file));
        if (pt::setup::IsPackage(file)) return UsePackage(file);
        std::error_code error;
        if (fs::is_regular_file(file, error) && LooksLikeGameDir(file.parent_path())) return UseFolder(file.parent_path());
        if (fs::is_directory(file, error)) return UseFolder(file);
        return {{}, ui_.T("pc_first_run_not_pkg", {Utf8(file)})};
    }

private:
    std::string Check(const fs::path& game) {
        const int missing = c_.missing_packages ? c_.missing_packages(game) : 0;
        if (missing == 0) return {};
        pt::LogWarn("first run: {} is not a complete P.T. ({})", Utf8(game), missing < 0 ? "its archives do not open" : std::format("{} core packages missing", missing));
        return ui_.T("pc_first_run_incomplete", {Utf8(game)});
    }

    std::vector<fs::path> Targets() const {
        std::vector<fs::path> targets;
        if (!c_.program_dir_read_only && !c_.program_dir.empty()) targets.push_back(c_.program_dir / "CUSA01127");
        if (!c_.user_dir.empty()) targets.push_back(c_.user_dir / "CUSA01127");
        return targets;
    }

    Outcome UsePackage(const fs::path& package) {
        const fs::path pkg = fs::absolute(package);
        pt::LogInfo("first run: PKG {} ({} bytes)", Utf8(pkg), FileSize(pkg));
        fs::path helper;
        std::error_code error;
        for (const auto& candidate : c_.helper_candidates) {
            if (fs::is_regular_file(candidate, error)) {
                helper = candidate;
                break;
            }
        }
        if (helper.empty()) {
            pt::LogError("first run: no PKG extraction helper (looked for {})", c_.helper_candidates.empty() ? std::string("nothing") : Utf8(c_.helper_candidates.front()));
            return {{}, ui_.T("pc_first_run_no_extractor", {c_.helper_candidates.empty() ? std::string() : Utf8(c_.helper_candidates.front())})};
        }
        const uint64_t pkg_bytes = FileSize(pkg);
        std::string tried;
        for (const fs::path& target : Targets()) {
            tried += "\n" + Utf8(target);
            if (fs::exists(target, error)) {
                if (!fs::is_directory(target, error) || !fs::is_empty(target, error)) {
                    pt::LogInfo("first run: {} already exists and is not empty; not used", Utf8(target));
                    continue;
                }
            }
            const fs::path parent = target.parent_path();
            fs::create_directories(parent, error);
            const fs::path staging = parent / std::format(".pt-extract-{}", pt::os::ProcessId());
            RemoveStale(parent);
            fs::remove_all(staging, error);
            if (!fs::create_directory(staging, error)) {
                pt::LogInfo("first run: {} is not writable ({}); not used", Utf8(parent), error.message());
                continue;
            }
            const auto space = fs::space(parent, error);
            const uint64_t needed = pkg_bytes + 256ull * 1024 * 1024;
            if (!error && space.available < needed) {
                fs::remove_all(staging, error);
                return {{}, ui_.T("pc_first_run_no_space", {std::format("{:.1f}", needed / (1024.0 * 1024.0 * 1024.0)), Utf8(parent)})};
            }
            return Extract(helper, pkg, pkg_bytes, staging, target);
        }
        return {{}, ui_.T("pc_first_run_no_target", {tried})};
    }

    static void RemoveStale(const fs::path& parent) {
        std::error_code error;
        std::vector<fs::path> stale;
        for (fs::directory_iterator it(parent, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error)) {
            const std::string name = Utf8(it->path().filename());
            if (!name.starts_with(".pt-extract-") || !it->is_directory(error) || pt::os::IsLink(it->path())) continue;
            const uint64_t pid = std::strtoull(name.c_str() + 12, nullptr, 10);
            if (pid && pid != pt::os::ProcessId() && !pt::os::ProcessAlive(pid)) stale.push_back(it->path());
        }
        for (const auto& dir : stale) {
            pt::LogInfo("first run: removing what an interrupted extraction left: {}", Utf8(dir));
            fs::remove_all(dir, error);
        }
    }

    Outcome Extract(const fs::path& helper, const fs::path& pkg, uint64_t pkg_bytes, const fs::path& staging, const fs::path& target) {
        const fs::path output = staging / "CUSA01127", log = staging / "extraction.log";
        pt::LogInfo("first run: extracting with {} into {}", Utf8(helper), Utf8(output));
#ifndef _WIN32
        setenv("DOTNET_SYSTEM_GLOBALIZATION_INVARIANT", "1", 1);
        {
            std::error_code mode_error;
            fs::permissions(helper, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, mode_error);
        }
#endif
        std::atomic<bool> cancel{false}, done{false};
        pt::os::ProcessResult result;
        std::thread worker([&] {
            result = pt::os::RunProcess(helper, {Utf8(pkg), Utf8(output)}, staging, log, cancel, std::chrono::hours(2));
            done = true;
        });
        const auto started = std::chrono::steady_clock::now();
        while (!done) {
            const double fraction = pkg_bytes ? static_cast<double>(OutputBytes(output)) / static_cast<double>(pkg_bytes) : 0.0;
            if (!ui_.Progress(fraction) && !cancel) {
                pt::LogInfo("first run: the player closed the progress window; the extraction is cancelled");
                cancel = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(c_.interactive ? 30 : 100));
        }
        worker.join();
        ui_.CloseWindow();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const std::string messages = ReadTail(log, 4000);
        for (size_t at = 0; at < messages.size();) {
            const size_t end = std::min(messages.find('\n', at), messages.size());
            std::string line = messages.substr(at, end - at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) pt::LogInfo("first run: extractor: {}", line);
            at = end + 1;
        }
        std::error_code error;
        auto fail = [&](std::string text) {
            fs::remove_all(staging, error);
            return Outcome{{}, std::move(text)};
        };
        if (!result.started) {
            pt::LogError("first run: the extraction helper did not start: {}", Utf8(helper));
            return fail(ui_.T("pc_first_run_no_extractor", {Utf8(helper)}));
        }
        if (result.cancelled) return fail(ui_.T("pc_first_run_cancelled"));
        if (result.timed_out || result.exit_code != 0) {
            pt::LogError("first run: the extraction failed after {:.1f} s (exit code {}{})", seconds, result.exit_code, result.timed_out ? ", timed out" : "");
            return fail(ui_.T("pc_first_run_extract_failed", {Utf8(pkg), ReadTail(log, 600)}));
        }
        pt::LogInfo("first run: extracted in {:.1f} s ({} bytes)", seconds, OutputBytes(output));
        if (!LooksLikeGameDir(output)) return fail(ui_.T("pc_first_run_incomplete", {Utf8(pkg)}));
        if (const std::string problem = Check(output); !problem.empty()) return fail(ui_.T("pc_first_run_incomplete", {Utf8(pkg)}));
        if (const auto picture = pt::setup::SystemPngFromPackage(pkg, "pic1.png")) {
            fs::create_directories(output / "sce_sys", error);
            std::ofstream file(output / "sce_sys" / "pic1.png", std::ios::binary);
            file.write(picture->data(), static_cast<std::streamsize>(picture->size()));
        }
        if (fs::exists(target, error)) fs::remove(target, error);
        fs::rename(output, target, error);
        if (error) {
            pt::LogError("first run: {} could not be moved to {}: {}", Utf8(output), Utf8(target), error.message());
            return fail(ui_.T("pc_first_run_extract_failed", {Utf8(pkg), error.message()}));
        }
        fs::remove_all(staging, error);
        pt::LogInfo("first run: the game files are in {}", Utf8(target));
        return {target, {}, true};
    }

    const Context& c_;
    Ui& ui_;
};

void Remember(const Context& c, const fs::path& game) {
    if (c.remembered_file.empty()) return;
    std::error_code error;
    fs::create_directories(c.remembered_file.parent_path(), error);
    std::ofstream out(c.remembered_file, std::ios::binary | std::ios::trunc);
    const std::u8string text = fs::absolute(game).u8string();
    out.write(reinterpret_cast<const char*>(text.data()), static_cast<std::streamsize>(text.size()));
    if (!out) pt::LogWarn("first run: {} could not be written; the screen asks again at the next start", Utf8(c.remembered_file));
}

}

bool LooksLikeGameDir(const fs::path& dir) {
    std::error_code error;
    return !dir.empty() && fs::is_regular_file(dir / "chunk1.psarc", error) && fs::is_regular_file(dir / "texture.qar", error);
}

fs::path FindGameFolder(const fs::path& picked) {
    if (picked.empty()) return {};
    if (LooksLikeGameDir(picked)) return picked;
    if (LooksLikeGameDir(picked / "CUSA01127")) return picked / "CUSA01127";
    std::vector<fs::path> level{picked};
    std::error_code error;
    for (int depth = 0; depth < 2 && !level.empty(); ++depth) {
        std::vector<fs::path> next;
        for (const auto& dir : level) {
            for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, error), end; !error && it != end && next.size() < 256;
                 it.increment(error)) {
                const fs::path name = it->path().filename();
                if (!it->is_directory(error) || name == "sce_sys" || name == "sce_module") continue;
                if (LooksLikeGameDir(it->path())) return it->path();
                next.push_back(it->path());
            }
            error.clear();
        }
        level = std::move(next);
    }
    return {};
}

int PlayerLanguage(const fs::path& user_dir) {
    if (!user_dir.empty()) {
        pt::game::SaveStore store;
        store.SetDirectory(user_dir, "PT_Save_Data");
        if (const auto save = store.Load()) return std::clamp(save->options.subtitle_language, 0, kLanguages - 1);
    }
    std::string tag = pt::os::GetEnv("PT_SYSTEM_LANGUAGE");
#ifdef _WIN32
    if (tag.empty()) tag = pt::game::SystemLanguageTag();
#else
    if (tag.empty()) {
        int count = 0;
        if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
            if (count > 0 && locales[0] && locales[0]->language) {
                tag = locales[0]->language;
                if (locales[0]->country) tag += std::string("-") + locales[0]->country;
            }
            SDL_free(locales);
        }
    }
#endif
    if (tag.empty()) return 0;
    return std::clamp(pt::game::DefaultOptionsForLocale(tag).subtitle_language, 0, kLanguages - 1);
}

std::string Text(std::string_view key, int language, std::initializer_list<std::string> args) {
    std::string text(pt::game::PcText(key, std::clamp(language, 0, kLanguages - 1)));
    int index = 1;
    for (const auto& arg : args) {
        const std::string token = "{" + std::to_string(index++) + "}";
        for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + arg.size())) text.replace(at, token.size(), arg);
    }
    return text;
}

Result Run(const Context& context) {
    auto script = TestScript();
    const bool scripted = script.has_value();
    Ui ui(context);
    Flow flow(context, ui);
    const auto opened = std::chrono::steady_clock::now();
    pt::LogInfo("first run: the game files screen (language {}{})", context.language, scripted ? ", answers from PT_TEST_FIRST_RUN" : "");
    for (;;) {
        Choice choice;
        if (scripted) {
            if (!script->empty()) {
                choice = script->front();
                script->pop_front();
            }
        } else {
            choice.kind = ui.Ask();
        }
        if (choice.kind == Kind::Quit) {
            pt::LogInfo("first run: the player quit the screen after {:.1f} s", std::chrono::duration<double>(std::chrono::steady_clock::now() - opened).count());
            return {};
        }
        if (choice.kind == Kind::Cancel) {
            pt::LogInfo("first run: the file window was closed without a choice");
            continue;
        }
        if (choice.kind != Kind::NoDialog && choice.path.empty() && !scripted) {
            const bool folder = choice.kind == Kind::Folder;
            choice.kind = ui.Pick(folder, choice.path);
            if (choice.kind == Kind::Cancel) {
                pt::LogInfo("first run: the {} window was closed without a choice", folder ? "folder" : "file");
                continue;
            }
        }
        if (choice.kind == Kind::NoDialog) {
            ui.Message(ui.T("pc_first_run_no_dialog") + "\n\n" + ui.Layout(), true);
            return {};
        }
        const Outcome outcome = choice.kind == Kind::Package ? flow.UsePackageFile(choice.path) : flow.UseFolder(choice.path);
        if (outcome.game_dir.empty()) {
            ui.Message(outcome.error);
            continue;
        }
        Remember(context, outcome.game_dir);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - opened).count();
        pt::LogInfo("first run: using {} ({})", Utf8(outcome.game_dir), outcome.extracted ? "extracted from the PKG" : "an extracted folder");
        return {outcome.game_dir, std::format("{} in the first-run screen after {:.1f} s", outcome.extracted ? "extracted from the PKG" : "a folder picked", seconds)};
    }
}

}
