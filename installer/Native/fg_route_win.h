#pragma once
#include <functional>
#include <sstream>

#include "engine/platform/json_value.h"
#include "engine/render/upscale/fg_unlock.h"

namespace pt::setup::fg_route {
namespace fs = std::filesystem;

struct Args {
    bool remove = false;
    bool reinstall = false;
    fs::path install;
    fs::path work;
    uint64_t pid = 0;
    int language = 0;
    std::vector<std::wstring> game_args;
};

inline std::string KindName(const Args& a) { return a.reinstall ? "reinstall" : a.remove ? "remove" : "install"; }
inline std::wstring Wide(std::string_view s) { return std::wstring(s.begin(), s.end()); }

inline void Note(const Args& a, const std::string& text) {
    std::error_code ec;
    if (!fs::is_directory(a.install / L"data", ec)) return;
    std::ofstream log(a.install / L"data" / L"pt-updater.log", std::ios::app | std::ios::binary);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    log << stamp << "fg route: " << text << "\n";
}

inline void WriteResult(const Args& a, int code, const std::string& detail) {
    std::ofstream out(a.work / std::string(pt::fg_unlock::kResultFile), std::ios::binary | std::ios::trunc);
    out << "kind " << KindName(a) << "\nexit " << code << "\ndetail " << detail << "\n";
}

inline std::string JsonText(const pt::json::Value& v, const char* name) {
    const auto it = v.members.find(name);
    return it != v.members.end() && !it->second.object && it->second.items.empty() ? it->second.text : std::string();
}

inline std::string FileSha(const fs::path& p) {
    try {
        return Hash(p);
    } catch (const std::exception&) {
        return {};
    }
}

inline fs::path ZipPath(const fs::path& work) {
    const std::string env = pt::os::GetEnv("PT_FG_TOOL_URL");
    const std::string url = env.empty() ? std::string(pt::fg_unlock::kToolUrl) : env;
    return work / pt::os::PathFromUtf8(url.substr(url.rfind('/') + 1));
}

inline std::string PreviousProxy(const fs::path& game) {
    std::ifstream in(game / Wide(pt::fg_unlock::kManifest), std::ios::binary);
    if (!in) return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    pt::json::Value root;
    return pt::json::Reader(text).Parse(root) && root.object ? JsonText(root, "proxy") : std::string();
}

inline int Tar(const fs::path& zip, const fs::path& dir, const std::string& member, const fs::path& log) {
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    std::atomic<bool> never{false};
    return pt::os::RunProcess(fs::path(system) / L"tar.exe", {"-xf", pt::os::PathToUtf8(zip), "-C", pt::os::PathToUtf8(dir), member}, dir, log, never,
                              std::chrono::minutes(5)).exit_code;
}

inline bool IsOurProxyFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data.find(std::string(pt::fg_unlock::kProxyMarker)) != std::string::npos;
}

inline bool IEq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

inline std::string ChooseProxy(const fs::path& game, std::string_view keep) {
    std::error_code ec;
    if (!keep.empty()) {
        const fs::path p = game / std::string(keep);
        if (!fs::exists(p, ec) || IsOurProxyFile(p)) return std::string(keep);
    }
    for (const char* name : pt::fg_unlock::kProxyCandidates) {
        if (IEq(name, pt::fg_unlock::kDlss5Proxy)) continue;
        const fs::path p = game / name;
        if (!fs::exists(p, ec) || IsOurProxyFile(p)) return name;
    }
    return {};
}

inline void RemoveFiles(const Args& a) {
    std::error_code ec;
    for (const char* name : pt::fg_unlock::kProxyCandidates) {
        if (std::string(name) == std::string(pt::fg_unlock::kDlss5Proxy)) continue;
        const fs::path p = a.install / name;
        if (fs::is_regular_file(p, ec) && IsOurProxyFile(p)) {
            fs::remove(p, ec);
            Note(a, std::string("removed proxy ") + name);
        }
    }
    fs::remove(a.install / Wide(pt::fg_unlock::kIni), ec);
    fs::remove_all(a.install / Wide(pt::fg_unlock::kDir), ec);
    fs::remove(a.install / Wide(pt::fg_unlock::kManifest), ec);
    for (const char* name : pt::fg_unlock::kProxyCandidates) {
        const fs::path backup = a.install / (std::string(name) + ".pt-fg-backup");
        if (fs::is_regular_file(backup, ec)) {
            fs::rename(backup, a.install / name, ec);
            Note(a, std::string("restored the original ") + name);
        }
    }
}

inline int DoInstall(const Args& a, std::string& detail) {
    std::error_code ec;
    const fs::path zip = ZipPath(a.work);
    if (!fs::is_regular_file(zip, ec)) {
        detail = "the package zip is not in the work folder";
        return -1;
    }
    fs::remove(a.work / L"manifest.json", ec);
    Tar(zip, a.work, std::string(pt::fg_unlock::kPackageManifest), a.work / L"tar.log");
    pt::json::Value pkg;
    std::string runtime;
    {
        std::ifstream in(a.work / L"manifest.json", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (pt::json::Reader(text).Parse(pkg) && pkg.object)
            if (const auto b = pkg.members.find("bundle"); b != pkg.members.end()) runtime = JsonText(b->second, "runtime_model");
    }
    const std::string proxy = ChooseProxy(a.install, PreviousProxy(a.install));
    if (proxy.empty()) {
        detail = "no free proxy name (version.dll, winmm.dll and dbghelp.dll are all taken by other software)";
        return -1;
    }
    RemoveFiles(a);
    const fs::path proxy_dest = a.install / proxy;
    if (fs::is_regular_file(proxy_dest, ec) && !IsOurProxyFile(proxy_dest))
        fs::rename(proxy_dest, a.install / (proxy + ".pt-fg-backup"), ec);
    Tar(zip, a.install, std::string(pt::fg_unlock::kIni), a.work / L"tar.log");
    const int folder_code = Tar(zip, a.install, std::string(pt::fg_unlock::kDir), a.work / L"tar.log");
    const bool root_proxy = proxy == std::string(pt::fg_unlock::kProxyCandidates[0]);
    const std::string proxy_member = root_proxy ? proxy : ("alternatives/" + proxy);
    fs::create_directories(a.work / L"proxy", ec);
    Tar(zip, a.work / L"proxy", proxy_member, a.work / L"tar.log");
    fs::remove(proxy_dest, ec);
    fs::copy_file(a.work / L"proxy" / pt::os::PathFromUtf8(proxy_member), proxy_dest, fs::copy_options::overwrite_existing, ec);
    if (ec || !fs::is_regular_file(proxy_dest, ec) || folder_code != 0) {
        detail = "the package could not be extracted (tar failed or the proxy did not land)";
        return -1;
    }
    std::ostringstream files;
    auto record = [&](const std::string& name) {
        const fs::path p = a.install / pt::os::PathFromUtf8(name);
        files << "    {\"name\": \"" << name << "\", \"size\": " << (fs::is_regular_file(p, ec) ? fs::file_size(p, ec) : 0)
              << ", \"sha256\": \"" << FileSha(p) << "\"},\n";
    };
    record(proxy);
    record(std::string(pt::fg_unlock::kIni));
    size_t n = 2;
    if (const auto fl = pkg.members.find("files"); fl != pkg.members.end()) {
        for (const auto& f : fl->second.items) {
            const std::string name = JsonText(f, "path");
            if (name.rfind(std::string(pt::fg_unlock::kDir) + "/", 0) != 0) continue;
            const std::string want = JsonText(f, "sha256"), got = FileSha(a.install / pt::os::PathFromUtf8(name));
            if (!got.empty() && !want.empty() && got != want) Note(a, "WARNING file " + name + " sha " + got + " does not match the package " + want);
            record(name);
            ++n;
        }
    }
    std::string tool_tag = "v" + std::string(pt::fg_unlock::kToolVersion), tool_kind = "pinned";
    {
        std::ifstream ts(a.work / "tool-source.txt");
        std::getline(ts, tool_kind);
        std::getline(ts, tool_tag);
        if (tool_kind.empty()) tool_kind = "pinned";
    }
    std::string fj = files.str();
    if (fj.size() >= 2) fj.erase(fj.size() - 2);
    std::ofstream out(a.install / Wide(pt::fg_unlock::kManifest), std::ios::binary | std::ios::trunc);
    out << "{\n  \"format\": 1,\n  \"complete\": true,\n  \"tool\": \"" << std::string(pt::fg_unlock::kToolName) << "\",\n  \"tool_source\": \"" << tool_kind
        << "\",\n  \"tool_tag\": \"" << tool_tag << "\",\n  \"runtime\": \"" << runtime << "\",\n  \"proxy\": \"" << proxy << "\",\n  \"files\": [\n" << fj << "\n  ]\n}\n";
    Note(a, "installed: proxy " + proxy + ", runtime " + runtime + ", " + std::to_string(n) + " files (tool " + tool_kind + " " + tool_tag + ")");
    detail = "installed proxy " + proxy + ", DLSS-G runtime " + runtime;
    return 0;
}

inline int RunStep(const Args& a, std::string& detail, bool wait = true) {
    if (wait && !win_route::WaitForGame(a.pid)) {
        detail = "P.T. was still running after 60 s";
        return -1;
    }
    std::string want(pt::fg_unlock::kToolZipSha256);
    {
        std::ifstream ts(a.work / "tool-source.txt");
        std::string kind, tag, sha;
        std::getline(ts, kind);
        std::getline(ts, tag);
        std::getline(ts, sha);
        if (sha.size() == 64) want = sha;
    }
    if (!a.remove) {
        const std::string got = FileSha(ZipPath(a.work));
        if (got != want) {
            detail = "the package zip SHA-256 " + (got.empty() ? "unreadable" : got) + " does not match what the download recorded " + want;
            Note(a, detail);
            return -1;
        }
    }
    if (a.remove) {
        RemoveFiles(a);
        detail = "removed";
        return 0;
    }
    if (a.reinstall) RemoveFiles(a);
    return DoInstall(a, detail);
}

inline int FgOp(const Args& a, bool wait, const std::function<void(UpdaterText)>& step) {
    step(a.remove ? UpdaterText::FgRemoving : a.reinstall ? UpdaterText::FgReinstalling : UpdaterText::FgInstalling);
    std::string detail;
    const int code = RunStep(a, detail, wait);
    WriteResult(a, code, detail);
    Note(a, KindName(a) + " exit " + std::to_string(code) + ": " + detail);
    if (code != 0) {
        step(UpdaterText::FgFailed);
        Sleep(3000);
    }
    return code;
}

inline std::optional<Args> ParseRoute(int argc, wchar_t** argv) {
    if (argc < 4 || std::wstring(argv[1]) != L"--fg-route") return std::nullopt;
    const std::wstring kind = argv[2];
    if (kind != L"install" && kind != L"remove" && kind != L"reinstall") return std::nullopt;
    Args args;
    args.remove = kind == L"remove";
    args.reinstall = kind == L"reinstall";
    args.install = FolderPath(argv[3]);
    for (int i = 4; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--pid" && i + 1 < argc) args.pid = _wcstoui64(argv[++i], nullptr, 10);
        else if (arg == L"--lang" && i + 1 < argc) args.language = std::clamp(_wtoi(argv[++i]), 0, kUpdaterLanguages - 1);
        else if (arg == L"--work" && i + 1 < argc) args.work = FolderPath(argv[++i]);
        else if (arg == L"--") { for (++i; i < argc; ++i) args.game_args.emplace_back(argv[i]); }
    }
    if (args.work.empty() || args.work.lexically_normal() != (args.install / L"data" / L"dlssg-unlock").lexically_normal()) return std::nullopt;
    return args;
}

inline int RunRoute(const Args& args) {
    static win_route::State state;
    state.args.install = args.install;
    state.args.language = args.language;
    state.note = [](const win_route::Args& a, const std::string& text) {
        Args fg;
        fg.install = a.install;
        Note(fg, text);
    };
    win_route::g_route = &state;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&common);
    const float scale = GetDpiForSystem() / 96.0f;
    state.font = CreateFontW(int(-15 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW cls{};
    cls.lpfnWndProc = win_route::RouteProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"PTRoute";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    RegisterClassW(&cls);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, int(500 * scale), int(110 * scale)};
    AdjustWindowRect(&bounds, style, FALSE);
    const std::wstring title = win_updater::WideText(UpdaterString(args.language, UpdaterText::FgTitle));
    state.window = CreateWindowExW(0, cls.lpszClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                                   bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, nullptr);
    auto control = [&](const wchar_t* kind, const wchar_t* text, DWORD extra, int x, int y, int w, int h) {
        HWND c = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | extra, int(x * scale), int(y * scale), int(w * scale), int(h * scale),
                                 state.window, nullptr, cls.hInstance, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        return c;
    };
    const UpdaterText status = args.remove ? UpdaterText::FgRemoving : args.reinstall ? UpdaterText::FgReinstalling : UpdaterText::FgInstalling;
    state.status = control(L"STATIC", win_updater::WideText(UpdaterString(args.language, status)).c_str(), 0, 20, 14, 460, 24);
    state.bar = control(PROGRESS_CLASSW, L"", PBS_MARQUEE, 20, 50, 460, 15);
    SendMessageW(state.bar, PBM_SETMARQUEE, TRUE, 30);
    ShowWindow(state.window, SW_SHOWNOACTIVATE);
    state.args.game_args = args.game_args;
    state.worker = std::thread([args] {
        FgOp(args, true, [](UpdaterText text) { PostMessageW(win_route::g_route->window, win_route::kStep, WPARAM(text), 0); });
        PostMessageW(win_route::g_route->window, win_route::kOver, 0, 0);
    });
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (state.worker.joinable()) state.worker.join();
    DeleteObject(state.font);
    CoUninitialize();
    return 0;
}

}
