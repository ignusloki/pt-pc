#pragma once
#include <functional>
#include <sstream>

#include "engine/platform/json_value.h"
#include "engine/render/upscale/neural_unlock.h"

namespace pt::setup::win_route {
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

constexpr UINT kStep = WM_APP + 21, kLine = WM_APP + 22, kOver = WM_APP + 23;
struct State {
    Args args;
    HWND window = nullptr, status = nullptr, line = nullptr, bar = nullptr;
    HFONT font = nullptr;
    std::thread worker;
    std::mutex mutex;
    std::wstring last_line;
    void (*note)(const Args&, const std::string&) = nullptr;
};
inline State* g_route = nullptr;

inline void RouteNote(const Args& args, const std::string& text) {
    std::error_code error;
    if (!fs::is_directory(args.install / L"data", error)) return;
    std::ofstream log(args.install / L"data" / L"pt-updater.log", std::ios::app | std::ios::binary);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    log << stamp << "dlss5 route: " << text << "\n";
}

inline std::string LastLine(const fs::path& log) {
    std::ifstream in(log, std::ios::binary);
    if (!in) return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (char& c : text) if (c == '\r') c = '\n';
    size_t end = text.size();
    while (end > 0) {
        while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == ' ')) --end;
        const size_t begin = text.rfind('\n', end ? end - 1 : 0);
        const size_t from = begin == std::string::npos ? 0 : begin + 1;
        std::string line = text.substr(from, end - from);
        while (!line.empty() && line.front() == ' ') line.erase(line.begin());
        if (!line.empty()) return line.size() > 90 ? line.substr(0, 87) + "..." : line;
        if (begin == std::string::npos) break;
        end = begin;
    }
    return {};
}

inline std::string KindName(const Args& a) { return a.reinstall ? "reinstall" : a.remove ? "remove" : "install"; }

inline void WriteResult(const Args& args, int exit_code, const std::string& detail) {
    std::ofstream out(args.work / std::string(nr_unlock::kResultFile), std::ios::binary | std::ios::trunc);
    out << "kind " << KindName(args) << "\nexit " << exit_code << "\ndetail " << detail << "\n";
}

inline int RunInstaller(const Args& a, const fs::path& tool, bool remove, const std::wstring& log_name, std::string& detail) {
    const fs::path game = a.install / L"pt.exe";
    std::vector<std::string> tool_args{pt::os::PathToUtf8(game)};
    if (remove) tool_args.push_back("--remove");
    else tool_args.insert(tool_args.end(), {"--route", std::string(nr_unlock::kRoute), "--no-gpu-pref"});
    const fs::path log = a.work / log_name;
    std::atomic<bool> never{false};
    const pt::os::ProcessResult run = pt::os::RunProcess(tool, tool_args, a.work, log, never, std::chrono::minutes(20));
    {
        std::ifstream in(log, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), line;
        for (char& c : text) if (c == '\r') c = '\n';
        std::istringstream lines(text);
        while (std::getline(lines, line))
            if (line.find_first_not_of(' ') != std::string::npos) RouteNote(a, "installer: " + line);
    }
    detail = !run.started ? "the installer could not be started" : run.timed_out ? "the installer ran for 20 minutes and was stopped" : LastLine(log);
    return run.started && !run.timed_out ? run.exit_code : -1;
}

inline std::string JsonText(const pt::json::Value& v, const char* name) {
    const auto it = v.members.find(name);
    return it != v.members.end() && !it->second.object && it->second.items.empty() ? it->second.text : std::string();
}

inline std::string InstallOwnReShade(const Args& a, std::string& detail) {
    const std::wstring dir(nr_unlock::kOwnReShadeDir.begin(), nr_unlock::kOwnReShadeDir.end());
    const std::wstring name(nr_unlock::kOwnReShadeFile.begin(), nr_unlock::kOwnReShadeFile.end());
    const fs::path own = a.install / dir / name, target = a.install / name, temp = a.install / (name + L".pt-tmp");
    std::error_code ec;
    if (!fs::is_regular_file(own, ec)) {
        detail = "the package has no " + pt::os::PathToUtf8(own);
        return {};
    }
    fs::copy_file(own, temp, fs::copy_options::overwrite_existing, ec);
    if (!ec) fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        detail = "could not replace " + pt::os::PathToUtf8(target);
        return {};
    }
    std::string sha, want;
    try {
        sha = Hash(target);
        want = Hash(own);
    } catch (const std::exception&) {}
    if (sha.empty() || sha != want) {
        detail = "the copied " + pt::os::PathToUtf8(target) + " does not match the package's";
        return {};
    }
    return sha;
}

inline std::string RecordedOwnReShade(const Args& a) {
    std::ifstream in(a.install / L"pt-dlss5-route.json", std::ios::binary);
    if (!in) return {};
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    pt::json::Value root;
    if (!pt::json::Reader(text).Parse(root) || !root.object) return {};
    return JsonText(root, "pt_reshade_sha256");
}

inline void RecordComponents(const Args& a, const std::string& own_reshade) {
    std::ifstream in(a.install / std::wstring(nr_unlock::kManifest.begin(), nr_unlock::kManifest.end()), std::ios::binary);
    if (!in) return;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    pt::json::Value root;
    if (!pt::json::Reader(text).Parse(root) || !root.object) return;
    const std::string route = JsonText(root, "path");
    std::string reshade, renodx, dlssnr;
    if (const auto c = root.members.find("components"); c != root.members.end() && c->second.object) {
        reshade = JsonText(c->second, "reshade");
        renodx = JsonText(c->second, "renodx");
        dlssnr = JsonText(c->second, "dlssnr");
    }
    std::vector<std::string> files;
    if (const auto f = root.members.find("files"); f != root.members.end())
        for (const auto& e : f->second.items)
            if (e.is_string && !e.text.empty()) files.push_back(e.text);
    std::string tool_kind = "pinned", tool_tag = "v" + std::string(nr_unlock::kToolVersion), dummy, tool_sha;
    {
        std::ifstream ts(a.work / "tool-source.txt");
        std::getline(ts, tool_kind);
        std::getline(ts, tool_tag);
        std::getline(ts, dummy);
        std::getline(ts, tool_sha);
        if (tool_kind.empty()) tool_kind = "pinned";
    }
    std::ostringstream json;
    json << "{\n  \"format\": 1,\n  \"tool\": \"" << std::string(nr_unlock::kToolName) << "\",\n  \"tool_source\": \"" << tool_kind
         << "\",\n  \"tool_tag\": \"" << tool_tag << "\",\n  \"route\": \"" << route << "\",\n"
         << "  \"pt_reshade_sha256\": \"" << own_reshade << "\",\n"
         << "  \"components\": {\"reshade\": \"" << reshade << "\", \"renodx\": \"" << renodx << "\", \"dlssnr\": \"" << dlssnr << "\"},\n"
         << "  \"files\": [\n";
    RouteNote(a, "components: ReShade " + reshade + ", renodx " + renodx + ", dlssnr " + dlssnr + " (tool " + tool_kind + " " + tool_tag + ")");
    for (size_t i = 0; i < files.size(); ++i) {
        const fs::path f = a.install / pt::os::PathFromUtf8(files[i]);
        std::error_code ec;
        const uint64_t size = fs::is_regular_file(f, ec) ? fs::file_size(f, ec) : 0;
        std::string sha;
        try { sha = Hash(f); } catch (const std::exception&) {}
        json << "    {\"name\": \"" << files[i] << "\", \"size\": " << size << ", \"sha256\": \"" << sha << "\"}" << (i + 1 < files.size() ? "," : "") << "\n";
        RouteNote(a, "  file " + files[i] + " " + std::to_string(size) + " bytes sha256 " + (sha.empty() ? "unreadable" : sha));
    }
    json << "  ]\n}\n";
    std::ofstream out(a.install / "pt-dlss5-route.json", std::ios::binary | std::ios::trunc);
    out << json.str();
}

inline bool WaitForGame(uint64_t pid) {
    if (!pid) return true;
    HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!game) return true;
    const DWORD waited = WaitForSingleObject(game, 60000);
    CloseHandle(game);
    return waited != WAIT_TIMEOUT;
}

inline int RouteStep(const Args& a, const std::function<void(UpdaterText)>& step) {
    RouteNote(a, KindName(a) + " started, tool " + std::string(nr_unlock::kToolVersion));
    const fs::path tool = a.work / (L"autopilot-" + std::wstring(nr_unlock::kToolVersion.begin(), nr_unlock::kToolVersion.end())) /
                          std::wstring(nr_unlock::kToolExe.begin(), nr_unlock::kToolExe.end());
    std::string sha;
    try {
        sha = Hash(tool);
    } catch (const std::exception&) {
    }
    std::string expected(nr_unlock::kToolExeSha256);
    {
        std::ifstream ts(a.work / "tool-source.txt");
        std::string line;
        for (int i = 0; i < 4 && std::getline(ts, line); ++i)
            if (i == 3 && line.size() == 64) expected = line;
    }
    if (sha != expected) {
        WriteResult(a, -1, "refused " + pt::os::PathToUtf8(tool) + ": SHA-256 " + (sha.empty() ? "unreadable" : sha) + ", expected " + expected);
        RouteNote(a, "tool refused, SHA-256 " + sha + " expected " + expected);
        step(UpdaterText::RouteFailed);
        Sleep(3000);
        return -1;
    }
    step(a.reinstall ? UpdaterText::RouteReinstalling : a.remove ? UpdaterText::RouteRemoving : UpdaterText::RouteInstalling);
    std::string detail;
    int code = 0;
    const std::string own_before = RecordedOwnReShade(a);
    if (a.reinstall) {
        std::string ignored;
        RunInstaller(a, tool, true, L"autopilot-reinstall-remove.log", ignored);
        code = RunInstaller(a, tool, false, L"autopilot-reinstall-install.log", detail);
    } else {
        code = RunInstaller(a, tool, a.remove, a.remove ? L"autopilot-remove.log" : L"autopilot-install.log", detail);
    }
    if (a.remove) {
        std::error_code ec;
        const fs::path dxgi = a.install / std::wstring(nr_unlock::kOwnReShadeFile.begin(), nr_unlock::kOwnReShadeFile.end());
        if (!own_before.empty() && fs::is_regular_file(dxgi, ec)) {
            std::string sha;
            try { sha = Hash(dxgi); } catch (const std::exception&) {}
            if (sha == own_before) {
                fs::remove(dxgi, ec);
                RouteNote(a, ec ? "the port's own ReShade (dxgi.dll) could not be deleted" : "the port's own ReShade (dxgi.dll) deleted");
            }
        }
        fs::remove(a.install / L"pt-dlss5-route.json", ec);
    } else if (code == 0) {
        step(UpdaterText::RouteVerifying);
        std::string why;
        const std::string own = InstallOwnReShade(a, why);
        RouteNote(a, own.empty() ? "the port's own ReShade was not put down (" + why + "); Autopilot's ReShade stays"
                                 : "the port's own ReShade (no user interface) put down as dxgi.dll, sha256 " + own);
        RecordComponents(a, own);
    }
    WriteResult(a, code, detail);
    RouteNote(a, "installer exit " + std::to_string(code) + ": " + detail);
    if (code != 0) {
        step(UpdaterText::RouteFailed);
        Sleep(3000);
    }
    return code;
}

inline void RouteWork(State& s) {
    const Args& a = s.args;
    auto step = [&](UpdaterText text) { if (s.window) PostMessageW(s.window, kStep, WPARAM(text), 0); };
    step(UpdaterText::Waiting);
    if (!WaitForGame(a.pid)) {
        RouteNote(a, KindName(a) + " started, tool " + std::string(nr_unlock::kToolVersion));
        WriteResult(a, -1, "P.T. was still running after 60 s");
        RouteNote(a, "P.T. was still running after 60 s");
        step(UpdaterText::RouteFailed);
        Sleep(3000);
        return;
    }
    RouteStep(a, step);
}

inline LRESULT CALLBACK RouteProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    State* s = g_route;
    if (message == WM_CTLCOLORSTATIC) {
        SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    if (message == kStep && s) {
        SetWindowTextW(s->status, win_updater::WideText(UpdaterString(s->args.language, UpdaterText(wp))).c_str());
        return 0;
    }
    if (message == kLine && s) {
        std::lock_guard lock(s->mutex);
        SetWindowTextW(s->line, s->last_line.c_str());
        return 0;
    }
    if (message == kOver && s) {
        if (s->worker.joinable()) s->worker.join();
        SetWindowTextW(s->status, win_updater::WideText(UpdaterString(s->args.language, UpdaterText::Starting)).c_str());
        win_updater::Args game;
        game.install = s->args.install;
        game.game_args = s->args.game_args;
        const win_updater::Started started = win_updater::StartGame(game, false);
        if (started.process) CloseHandle(started.process);
        (s->note ? s->note : RouteNote)(s->args, started.ok ? "P.T. started again" : "P.T. could not be started again");
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_CLOSE) return 0;
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

inline int RunRoute(const Args& args) {
    static State state;
    state.args = args;
    g_route = &state;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&common);
    const float scale = GetDpiForSystem() / 96.0f;
    state.font = CreateFontW(int(-15 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW cls{};
    cls.lpfnWndProc = RouteProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"PTRoute";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    RegisterClassW(&cls);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, int(500 * scale), int(85 * scale)};
    AdjustWindowRect(&bounds, style, FALSE);
    const std::wstring title = win_updater::WideText(UpdaterString(args.language, UpdaterText::RouteTitle));
    state.window = CreateWindowExW(args.language == 9 ? WS_EX_LAYOUTRTL : 0, cls.lpszClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                   bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, nullptr);
    auto control = [&](const wchar_t* kind, const wchar_t* text, DWORD extra, int x, int y, int w, int h) {
        HWND c = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | extra, int(x * scale), int(y * scale), int(w * scale), int(h * scale),
                                 state.window, nullptr, cls.hInstance, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        return c;
    };
    state.status = control(L"STATIC", win_updater::WideText(UpdaterString(args.language, UpdaterText::Waiting)).c_str(), 0, 20, 14, 460, 24);
    state.bar = control(PROGRESS_CLASSW, L"", PBS_MARQUEE, 20, 50, 460, 15);
    SendMessageW(state.bar, PBM_SETMARQUEE, TRUE, 30);
    ShowWindow(state.window, SW_SHOWNOACTIVATE);
    state.worker = std::thread([] {
        RouteWork(*g_route);
        PostMessageW(g_route->window, kOver, 0, 0);
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

inline std::optional<Args> ParseRoute(int argc, wchar_t** argv) {
    if (argc < 4 || std::wstring(argv[1]) != L"--dlss5-route") return std::nullopt;
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
        else if (arg == L"--") {
            for (++i; i < argc; ++i) args.game_args.emplace_back(argv[i]);
        }
    }
    if (args.work.empty() || args.work.lexically_normal() != (args.install / L"data" / L"dlss5-unlock").lexically_normal()) return std::nullopt;
    return args;
}

}
