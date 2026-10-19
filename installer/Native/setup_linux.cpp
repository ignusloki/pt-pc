#include "engine/platform/os.h"
#include "engine/platform/self_integrity.h"
#include "engine/platform/update_check.h"
#include "setup_core.h"
#include "updater_core.h"
#include "updater_text.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

using namespace pt::setup;

namespace {
constexpr const char* kProduct = "P.T. PC Port";
constexpr const char* kSlug = "pt-pc-port";
std::atomic<bool> cancel{false};

std::string HexDigest(const unsigned char digest[32]) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 32; ++i) {
        out += hex[digest[i] >> 4];
        out += hex[digest[i] & 15];
    }
    return out;
}
std::string HashFile(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    unsigned char digest[32];
    if (!input.eof() && !input) throw std::runtime_error("Could not verify file integrity.");
    pt::integrity::Sha256(bytes.data(), bytes.size(), digest);
    return HexDigest(digest);
}
std::vector<unsigned char> Payload() {
    std::ifstream self("/proc/self/exe", std::ios::binary);
    const auto size = pt::trailer::BodySize("/proc/self/exe");
    if (size < 24) throw std::runtime_error("Installer payload missing.");
    char trailer[24];
    self.seekg(std::streamoff(size - 24));
    self.read(trailer, 24);
    uint64_t offset = 0, length = 0;
    std::memcpy(&offset, trailer + 8, 8);
    std::memcpy(&length, trailer + 16, 8);
    if (std::memcmp(trailer, "PTPAYLD1", 8) != 0 || offset + length + 24 != size) throw std::runtime_error("Installer payload missing.");
    std::vector<unsigned char> data(length);
    self.seekg(std::streamoff(offset));
    self.read(reinterpret_cast<char*>(data.data()), std::streamsize(length));
    if (!self) throw std::runtime_error("Installer payload missing.");
    return data;
}
void CheckParents(const fs::path& destination) {
    std::error_code canonical_error;
    fs::path resolved = fs::absolute(destination);
    fs::path existing = resolved;
    while (!existing.empty() && !fs::exists(existing, canonical_error) && existing.parent_path() != existing) existing = existing.parent_path();
    if (existing != resolved) {
        const fs::path real = fs::canonical(existing, canonical_error);
        if (!canonical_error) resolved = real / fs::relative(resolved, existing, canonical_error);
    } else if (fs::is_symlink(resolved, canonical_error)) {
        throw std::runtime_error("Choose a destination without symbolic links.");
    } else {
        const fs::path real = fs::canonical(resolved, canonical_error);
        if (!canonical_error) resolved = real;
    }
    for (auto p = resolved; !p.empty();) {
        std::error_code error;
        if (fs::is_symlink(p, error)) throw std::runtime_error("Choose a destination without symbolic links.");
        const auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
}
void VerifyIntegrity() {
    CheckCancel();
    if (!pt::integrity::IntegrityOk()) throw std::runtime_error("This setup file is damaged or was modified. Please download it again.");
}
void MarkExecutables(const fs::path& root) {
    for (const char* name : {"pt", "pt-updater", "extractor/PT.PkgExtract", "texture-tools/realesrgan-ncnn-vulkan"}) {
        std::error_code error;
        if (fs::is_regular_file(root / name, error)) fs::permissions(root / name, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, error);
    }
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().filename().string().find(".so") != std::string::npos) {
            std::error_code error;
            fs::permissions(entry.path(), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, error);
        }
    }
}
void Extract(const fs::path& staging, const fs::path& package) {
    const fs::path assets = staging / "CUSA01127";
    const uint64_t before = progress.done;
    std::atomic<bool> finished{false};
    std::thread poll([&] {
        while (!finished) {
            progress.At(before + FolderBytes(assets));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
    const auto result = pt::os::RunProcess(staging / "extractor" / "PT.PkgExtract", {fs::absolute(package).string(), assets.string()}, staging,
                                           staging / "install-extraction.log", cancel, std::chrono::hours(2));
    finished = true;
    poll.join();
    if (!result.started) throw std::runtime_error("Could not start the PKG extraction helper.");
    CheckCancel();
    if (result.exit_code != 0) {
        std::ifstream input(staging / "install-extraction.log");
        std::string details((std::istreambuf_iterator<char>(input)), {});
        throw std::runtime_error("PKG extraction failed. " + details.substr(0, 600));
    }
}
void WriteDesktop(const fs::path& file, const fs::path& destination) {
    std::ofstream out(file);
    out << "[Desktop Entry]\nType=Application\nName=" << kProduct << "\nComment=" << kProduct << "\nExec=\""
        << (destination / "pt").string() << "\"\nPath=" << destination.string() << "\nTerminal=false\nCategories=Game;\n";
    std::error_code error;
    if (fs::is_regular_file(destination / "icon0.png", error)) out << "Icon=" << (destination / "icon0.png").string() << "\n";
}
void Shortcut(const fs::path& destination) {
    const char* home = std::getenv("HOME");
    if (!home) return;
    const fs::path dir = fs::path(home) / ".local" / "share" / "applications";
    std::error_code error;
    fs::create_directories(dir, error);
    fs::path file = dir / (std::string(kSlug) + ".desktop");
    if (fs::exists(file)) file = dir / (std::string(kSlug) + "-" + destination.filename().string() + ".desktop");
    if (!fs::exists(file)) WriteDesktop(file, destination);
    const fs::path desktop = fs::path(home) / "Desktop";
    if (fs::is_directory(desktop, error)) {
        fs::path link = desktop / (std::string(kProduct) + ".desktop");
        if (fs::exists(link)) link = desktop / (std::string(kProduct) + " " + destination.filename().string() + ".desktop");
        if (!fs::exists(link)) {
            WriteDesktop(link, destination);
            ::chmod(link.c_str(), 0755);
        }
    }
}
std::vector<fs::path> KnownFolders() {
    std::vector<fs::path> folders;
    if (const char* home = std::getenv("HOME")) {
        for (const char* name : {"Downloads", "Desktop", "Documents", "Games"}) folders.push_back(fs::path(home) / name);
    }
    return folders;
}
InstallOutcome Install(const fs::path& input, const fs::path& destination, bool shortcut) {
    InstallSteps steps;
    steps.version = std::string(pt::update::CurrentVersion());
    std::random_device random;
    char id[17];
    std::snprintf(id, sizeof(id), "%08x%08x", random(), random());
    steps.unique_id = id;
    steps.check_parents = CheckParents;
    steps.verify_integrity = VerifyIntegrity;
    auto payload = std::make_shared<std::vector<unsigned char>>();
    steps.payload_bytes = [payload] {
        if (payload->empty()) *payload = Payload();
        return PayloadBytes(payload->data(), payload->size());
    };
    steps.unpack = [payload](const fs::path& staging) {
        if (payload->empty()) *payload = Payload();
        auto files = UnpackPayload(payload->data(), payload->size(), staging, HashFile);
        MarkExecutables(staging);
        return files;
    };
    steps.hash_file = HashFile;
    steps.entries = [payload] {
        if (payload->empty()) *payload = Payload();
        return ListPayload(payload->data(), payload->size());
    };
    steps.fetch = [payload](const fs::path& folder, const std::vector<PayloadEntry>& wanted) {
        if (payload->empty()) *payload = Payload();
        std::set<std::string> names;
        for (const auto& entry : wanted) names.insert(entry.path);
        auto files = UnpackPayload(payload->data(), payload->size(), folder, HashFile, [&names](const std::string& name) { return names.count(name) > 0; });
        MarkExecutables(folder);
        return files;
    };
    if (pt::update::TestOverrides()) {
        if (const char* at = std::getenv("PT_UPDATE_TEST_FAIL_AT")) steps.fail_at = std::atoi(at);
        if (const char* at = std::getenv("PT_UPDATE_TEST_PAUSE_AT")) steps.pause_at = std::atoi(at);
        if (const char* file = std::getenv("PT_UPDATE_TEST_CORRUPT")) steps.corrupt_after_swap = file;
    }
    steps.extract = Extract;
    steps.shortcut = Shortcut;
    steps.process_alive = [](uint64_t pid) { return pt::os::ProcessAlive(pid); };
    steps.known_folders = KnownFolders;
    return RunInstall(input, destination, shortcut, steps);
}
std::string Outcome(const InstallOutcome& outcome) {
    std::string text = outcome.updated
        ? "Updated to version " + std::string(pt::update::CurrentVersion()) + ": " + std::to_string(outcome.swap.replaced) + " files replaced, " +
              std::to_string(outcome.swap.added) + " added, " + std::to_string(outcome.swap.removed) + " removed" +
              (outcome.archives_restored ? ", game archives restored" : "") + ". Your settings and saves are unchanged."
        : "Installed. Start pt from the install folder or the application menu.";
    if (!outcome.notes.empty()) text += " Your copy differs from the tested US release; install-notes.txt in the install folder lists how.";
    if (outcome.title_picture_missing)
        text += " The start picture (sce_sys/pic1.png) is still missing: run the setup again with your PKG or dump folder to add it.";
    return text;
}
enum class Dialogs { None, Zenity, Kdialog };
Dialogs FindDialogs() {
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) return Dialogs::None;
    if (std::system("command -v zenity >/dev/null 2>&1") == 0) return Dialogs::Zenity;
    if (std::system("command -v kdialog >/dev/null 2>&1") == 0) return Dialogs::Kdialog;
    return Dialogs::None;
}
std::string Quote(const std::string& text) {
    std::string out = "'";
    for (char c : text) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}
std::string RunDialog(const std::string& command) {
    std::string out;
    if (FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r")) {
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), pipe)) out += buffer;
        pclose(pipe);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}
std::string HomeDir() {
    const char* home = std::getenv("HOME");
    return home ? home : "/";
}
std::string PickDirectory(Dialogs d, const std::string& title) {
    return d == Dialogs::Zenity ? RunDialog("zenity --file-selection --directory --title=" + Quote(title))
                                : RunDialog("kdialog --title " + Quote(title) + " --getexistingdirectory " + Quote(HomeDir()));
}
std::string PickPackage(Dialogs d, const std::string& title) {
    return d == Dialogs::Zenity ? RunDialog("zenity --file-selection --title=" + Quote(title) + " --file-filter='PS4 package | *.pkg *.PKG'")
                                : RunDialog("kdialog --title " + Quote(title) + " --getopenfilename " + Quote(HomeDir()) + " 'PS4 package (*.pkg *.PKG)'");
}
std::string AskText(Dialogs d, const std::string& text, const std::string& fallback) {
    return d == Dialogs::Zenity ? RunDialog("zenity --entry --title=" + Quote(kProduct) + " --text=" + Quote(text) + " --entry-text=" + Quote(fallback))
                                : RunDialog("kdialog --title " + Quote(kProduct) + " --inputbox " + Quote(text) + " " + Quote(fallback));
}
bool AskYesNo(Dialogs d, const std::string& text) {
    const std::string command = d == Dialogs::Zenity ? "zenity --question --title=" + Quote(kProduct) + " --text=" + Quote(text)
                                                     : "kdialog --title " + Quote(kProduct) + " --yesno " + Quote(text);
    return std::system((command + " 2>/dev/null").c_str()) == 0;
}
void ShowMessage(Dialogs d, bool error, const std::string& text) {
    if (d == Dialogs::Zenity) RunDialog(std::string("zenity ") + (error ? "--error" : "--info") + " --title=" + Quote(kProduct) + " --text=" + Quote(text));
    else if (d == Dialogs::Kdialog) RunDialog("kdialog --title " + Quote(kProduct) + (error ? " --error " : " --msgbox ") + Quote(text));
}
std::string ExpandHome(std::string path) {
    while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
    if (path == "~" || path.starts_with("~/")) path = HomeDir() + path.substr(1);
    return path;
}
std::string Ask(const std::string& question, const std::string& fallback) {
    std::cout << question << (fallback.empty() ? "" : " [" + fallback + "]") << ": " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return line.empty() ? fallback : line;
}
int Interactive() {
    const bool terminal = isatty(0);
    const Dialogs dialogs = terminal ? Dialogs::None : FindDialogs();
    const bool gui = dialogs != Dialogs::None;
    const char* home = std::getenv("HOME");
    const std::string default_dest = home ? (fs::path(home) / ".local" / "share" / kSlug).string() : "";
    std::cout << kProduct << " setup, version " << pt::update::CurrentVersion() << "\n";
    pt::update::Checker updates;
    updates.Start();
    std::string input, destination;
    if (gui) {
        input = PickDirectory(dialogs, "Select the P.T. game folder from your dump (Cancel to pick a PKG)");
        if (input.empty()) input = PickPackage(dialogs, "Select your P.T. fake PKG");
        if (input.empty()) return 1;
        destination = ExpandHome(AskText(dialogs, "Installation folder (a new or empty one, or an existing install to update)", default_dest));
        if (destination.empty()) return 1;
    } else if (terminal) {
        std::cout << "Select your P.T. fake PKG or dumped game folder. No game assets are included.\n";
        destination = ExpandHome(Ask("Installation folder (a new or empty one, or an existing install to update)", default_dest));
        input = ExpandHome(Ask("PKG file or game folder (empty to keep the game files of an existing install)", ""));
    } else {
        std::cerr << "Run this setup from a terminal, or install zenity or kdialog for file pickers.\n";
        return 2;
    }
    struct Line { std::string text; int percent = -1; bool open = false; };
    static Line line;
    hooks.report = [](const std::string& text) {
        if (line.open) std::cout << "\n";
        line = {text, -1, true};
        std::cout << text << std::flush;
    };
    hooks.progress = [](uint64_t done, uint64_t total) {
        const int percent = total ? int(done * 100 / total) : 0;
        if (!total || percent == line.percent) return;
        line.percent = percent;
        line.open = true;
        std::cout << "\r" << line.text << " " << percent << "%" << std::flush;
    };
    hooks.cancelled = [] { return cancel.load(); };
    std::string result;
    int code = 0;
    try {
        const auto question = UpdateQuestion(InspectInstall(destination), std::string(pt::update::CurrentVersion()));
        if (!question.empty()) {
            const bool yes = gui ? AskYesNo(dialogs, question) : Ask(question + " (y/n)", "n").starts_with("y");
            if (!yes) return 1;
        }
        result = Outcome(Install(input, destination, true));
    } catch (const std::exception& e) {
        result = e.what();
        code = 1;
    }
    for (int i = 0; i < 20 && !updates.Done(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (const auto newer = updates.Newer()) result += "\nVersion " + newer->version + " is available: " + newer->url;
    if (line.open) std::cout << "\n";
    std::cout << result << "\n";
    if (gui) ShowMessage(dialogs, code != 0, result);
    return code;
}

struct LinuxPackage {
    std::vector<unsigned char> bytes;
    std::string sha256;
};
int GameUpdate(int argc, char** argv) {
    fs::path install = FolderPath(argv[2]);
    uint64_t pid = 0;
    int language = 0;
    std::vector<std::string> game_args;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--pid" && i + 1 < argc) pid = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--lang" && i + 1 < argc) language = std::clamp(std::atoi(argv[++i]), 0, kUpdaterLanguages - 1);
        else if (arg == "--") {
            for (++i; i < argc; ++i) game_args.emplace_back(argv[i]);
        }
    }
    fs::path result_file;
    if (pt::update::TestOverrides())
        if (const char* result = std::getenv("PT_UPDATE_TEST_RESULT"); result && *result) result_file = result;
    const Dialogs dialogs = result_file.empty() ? FindDialogs() : Dialogs::None;
    FILE* zenity = dialogs == Dialogs::Zenity
                       ? popen(("zenity --progress --auto-close --no-cancel --title=" + Quote(UpdaterString(language, UpdaterText::Title)) + " --text=" +
                                Quote(UpdaterString(language, UpdaterText::Waiting)) + " 2>/dev/null")
                                   .c_str(),
                               "w")
                       : nullptr;
    std::string version;
    auto show = [&](UpdaterText key, int percent) {
        const std::string text = UpdaterString(language, key, version);
        if (zenity) {
            std::fprintf(zenity, "# %s\n", text.c_str());
            if (percent >= 0) std::fprintf(zenity, "%d\n", percent);
            std::fflush(zenity);
        }
        std::cout << "\r" << text << (percent >= 0 ? " " + std::to_string(percent) + "%" : std::string()) << std::flush;
    };
    static std::atomic<int> last_percent{-1};
    static UpdaterText current = UpdaterText::Waiting;
    hooks.progress = [&](uint64_t done, uint64_t total) {
        if (!total) return;
        const int percent = int(done * 100 / total);
        if (percent != last_percent.exchange(percent)) show(current, percent);
    };
    hooks.cancelled = [] { return cancel.load(); };
    std::string page, detail;
    enum { Updated, Failed, RolledBack, Broken } outcome = Failed;
    bool start_old = false;
    auto start_game = [&]() -> pid_t {
        std::vector<std::string> owned{(install / "pt").string()};
        owned.insert(owned.end(), game_args.begin(), game_args.end());
        std::vector<char*> args;
        for (auto& arg : owned) args.push_back(arg.data());
        args.push_back(nullptr);
        const pid_t child = fork();
        if (child == 0) {
            setsid();
            if (chdir(install.c_str()) != 0) _exit(127);
            execv(owned[0].c_str(), args.data());
            _exit(127);
        }
        return child;
    };
    try {
        current = UpdaterText::Waiting;
        show(current, -1);
        for (int i = 0; pid && i < 600 && pt::os::ProcessAlive(pid); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (pid && pt::os::ProcessAlive(pid)) throw std::runtime_error("P.T. is still running.");
        GameUpdatePlatform platform;
        char id[17];
        std::random_device random;
        std::snprintf(id, sizeof(id), "%08x%08x", random(), random());
        platform.unique_id = id;
        platform.pid = uint64_t(getpid());
        platform.check_parents = CheckParents;
        auto package = std::make_shared<LinuxPackage>();
        platform.open_payload = [package](const fs::path& file) -> std::pair<const unsigned char*, size_t> {
            std::ifstream in(file, std::ios::binary);
            package->bytes.assign(std::istreambuf_iterator<char>(in), {});
            unsigned char digest[32];
            const auto& b = package->bytes;
            size_t body = b.size();
            if (b.size() >= pt::trailer::kFooterSize)
                if (const auto footer = pt::trailer::Parse(b.data() + b.size() - pt::trailer::kFooterSize, b.size())) body = size_t(footer->body());
            pt::integrity::Sha256(package->bytes.data(), body, digest);
            package->sha256 = HexDigest(digest);
            if (body < 24 || std::memcmp(b.data() + body - 24, "PTPAYLD1", 8) != 0) throw std::runtime_error("The downloaded package holds no payload.");
            uint64_t offset = 0, length = 0;
            std::memcpy(&offset, b.data() + body - 16, 8);
            std::memcpy(&length, b.data() + body - 8, 8);
            if (offset + length + 24 != body) throw std::runtime_error("The downloaded package holds no payload.");
            return {b.data() + offset, size_t(length)};
        };
        platform.hash_file = HashFile;
        platform.hash_package = [package](const fs::path& file) { return package->bytes.empty() ? HashFile(file) : package->sha256; };
        platform.after_unpack = MarkExecutables;
        platform.close_payload = [package] { package->bytes = {}; };
        platform.process_alive = [](uint64_t p) { return pt::os::ProcessAlive(p); };
        platform.known_folders = KnownFolders;
        platform.phase = [&](UpdatePhase phase) {
            current = phase == UpdatePhase::Checking      ? UpdaterText::Checking
                      : phase == UpdatePhase::Downloading ? UpdaterText::Downloading
                      : phase == UpdatePhase::Verifying   ? UpdaterText::Verifying
                      : phase == UpdatePhase::Installing  ? UpdaterText::Installing
                                                          : UpdaterText::Starting;
            last_percent = -1;
            show(current, -1);
        };
        if (pt::update::TestOverrides())
            if (const char* fail = std::getenv("PT_UPDATE_TEST_FAIL_AT")) platform.fail_at = std::atoi(fail);
        GameUpdateResult result;
        try {
            result = ApplyGameUpdate(install, platform, page);
        } catch (const std::exception& e) {
            detail = e.what();
            outcome = fs::exists(JournalFile(install)) && pt::update_journal::Read(JournalFile(install)) ? Broken : Failed;
            start_old = outcome == Failed;
            throw;
        }
        version = result.version;
        current = UpdaterText::Starting;
        show(current, 100);
        const pid_t game = start_game();
        std::function<std::optional<int>()> exit_code = [game]() -> std::optional<int> {
            int status = 0;
            if (game <= 0) return 1;
            if (waitpid(game, &status, WNOHANG) != game) return std::nullopt;
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        };
        int code = 0;
        const StartResult started = WaitForStart(result.outcome.journal, exit_code, std::chrono::seconds(120), &code);
        if (started == StartResult::Started) {
            FinishGameUpdate(result.outcome.journal);
            outcome = Updated;
        } else {
            detail = "the new version stopped at its start (exit code " + std::to_string(code) + ")";
            std::string why;
            if (RollBackGameUpdate(result.outcome.journal, &why)) {
                outcome = RolledBack;
                start_old = true;
            } else {
                outcome = Broken;
                detail += "; " + why;
            }
        }
    } catch (const std::exception& e) {
        if (detail.empty()) {
            detail = e.what();
            start_old = true;
        }
    }
    if (zenity) pclose(zenity);
    std::cout << "\n";
    if (!result_file.empty()) {
        static const char* const kinds[] = {"PASS updated", "FAIL failed", "FAIL rolled-back", "FAIL broken"};
        std::ofstream(result_file) << kinds[outcome] << " " << version << "\npage " << page << "\ndetail " << detail << "\n";
    } else if (outcome != Updated) {
        const UpdaterText main = outcome == RolledBack ? UpdaterText::RolledBack : outcome == Broken ? UpdaterText::Broken : UpdaterText::Failed;
        std::string text = UpdaterString(language, main) + "\n" + UpdaterString(language, UpdaterText::Fallback) + (page.empty() ? "" : "\n" + page) +
                           "\n\n" + UpdaterString(language, UpdaterText::Details) + " " + detail;
        std::cout << text << "\n";
        ShowMessage(FindDialogs(), true, text);
    }
    if (start_old && result_file.empty()) start_game();
    return outcome == Updated ? 0 : 1;
}
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--game-update") return GameUpdate(argc, argv);
    if (argc == 1) return Interactive();
    std::string mode = argv[1];
    const char* result_file = argv[argc - 1];
    int result = 0;
    try {
        if (mode == "--install" && argc == 5) {
            hooks.cancelled = [] { return cancel.load(); };
            static ProgressTrace trace;
            hooks.progress = [](uint64_t done, uint64_t total) { trace.Note(done, total); };
            if (!ResultPathWritable(argv[4])) throw std::runtime_error("refusing to overwrite the result file " + std::string(argv[4]));
            const auto outcome = Install(argv[2], argv[3], false);
            std::ofstream out(argv[4]);
            out << (outcome.updated ? "PASS updated" : "PASS installed");
            if (outcome.updated)
                out << "\nfrom " << (outcome.old_version.empty() ? "older install" : outcome.old_version) << " replaced " << outcome.swap.replaced << " added "
                    << outcome.swap.added << " removed " << outcome.swap.removed << " archives " << (outcome.archives_restored ? "restored" : "kept")
                    << "\nunchanged " << outcome.unchanged << " verified " << outcome.verified << " repaired " << outcome.repaired << " written "
                    << outcome.written_bytes << " bytes";
            for (const auto& note : outcome.notes) out << "\nnote: " << note;
            out << "\n" << trace.Summary();
        } else if (mode == "--verify-integrity" && argc == 3) {
            if (!ResultPathWritable(argv[2])) throw std::runtime_error("refusing to overwrite the result file " + std::string(argv[2]));
            VerifyIntegrity();
            std::ofstream(argv[2]) << "PASS integrity verified";
        } else if (mode == "--check-update" && argc == 3) {
            pt::update::Checker updates;
            updates.Start();
            for (int i = 0; i < 200 && !updates.Done(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const auto newer = updates.Newer();
            std::ofstream(argv[2]) << "url " << pt::update::ManifestUrl() << "\nthis " << pt::update::CurrentVersion() << "\ndone " << updates.Done()
                                   << "\nnewer " << (newer ? newer->version + " " + newer->url : std::string("none"));
        } else if (mode == "--self-test" && argc == 3) {
            bool rejected = false;
            try { Contained("/tmp/test", "../escape"); } catch (...) { rejected = true; }
            if (!rejected) throw std::runtime_error("Traversal accepted");
            if (pt::update::CurrentVersion() == "0.0.0-dev" || pt::update::ManifestUrl().find("github.com/") == std::string::npos) throw std::runtime_error("Built without pt_version.h or with a placeholder manifest address");
            const fs::path root = fs::temp_directory_path() / ("pt-setup-selftest-" + std::to_string(getpid()));
            const fs::path game = root / "dump" / "CUSA01127-app";
            fs::create_directories(game);
            auto write = [](const fs::path& file, const std::string& bytes) { std::ofstream(file, std::ios::binary) << bytes; };
            std::string qar(0x40, '\0');
            qar[0x40 - 0x24 + 0x16] = 'a';
            qar[0x40 - 0x24 + 0x17] = 'q';
            write(game / "chunk1.psarc", "PSAR" + std::string(60, '\0'));
            write(game / "texture.qar", qar);
            write(game / "pathid_list_ps4.bin", "/Assets/sh/level/pt14_hallway/" + std::string(40, 'x'));
            std::string failures;
            try {
                const auto found = ResolveSource(root);
                if (found.kind != SourceKind::Folder || found.path != game || !found.files.notes.empty()) failures += " folder-not-found";
            } catch (const std::exception& e) { failures += std::string(" folder:") + e.what(); }
            write(game / "chunk1.psarc", std::string(64, 'Z'));
            rejected = false;
            try { ResolveSource(game); } catch (...) { rejected = true; }
            if (!rejected) failures += " encrypted-accepted";
            GameFiles europe;
            europe.title = "CUSA01114";
            europe.pathid = game / "pathid_list_ps4.bin";
            if (!ConfirmPt(europe) || europe.notes.empty()) failures += " region-refused";
            failures += SelfTestIcon(root / "icon");
            std::error_code error;
            fs::remove_all(root, error);
            failures += SelfTestUpdate(root / "update");
            failures += SelfTestTitlePictureUpdate(root / "title-picture", HashFile);
            failures += SelfTestJournalRecovery(root / "journal");
            failures += SelfTestFileUpdate(root / "files", HashFile);
            failures += SelfTestUnicodePaths(root);
            fs::remove_all(root, error);
            if (pt::update::CompareVersions("0.10.0", "0.9.2") <= 0) failures += " version-order";
            if (!failures.empty()) throw std::runtime_error("Self test failed:" + failures);
            std::ofstream(argv[2]) << "PASS path containment, input formats, update manifest and in-place update";
        } else {
            std::cerr << "usage: " << argv[0] << " [--install <pkg or folder> <new folder> <result file> | --verify-integrity <file> | --check-update <file> | --self-test <file>]\n";
            result = 2;
        }
    } catch (const std::exception& e) {
        result = 1;
        std::cerr << e.what() << "\n";
        if (ResultPathWritable(result_file)) std::ofstream(result_file) << e.what();
    }
    return result;
}
