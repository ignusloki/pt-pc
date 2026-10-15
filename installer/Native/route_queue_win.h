#pragma once
#include "engine/render/upscale/route_queue.h"

namespace pt::setup::route_queue_win {
namespace fs = std::filesystem;
namespace rq = pt::route_queue;

struct Args {
    fs::path install, nr_work, fg_work;
    uint64_t pid = 0;
    int language = 0;
    rq::Queue queue;
    std::vector<std::wstring> game_args;
};

inline void QueueNote(const fs::path& install, const std::string& text) {
    std::error_code error;
    if (!fs::is_directory(install / L"data", error)) return;
    std::ofstream log(install / L"data" / L"pt-updater.log", std::ios::app | std::ios::binary);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    log << stamp << "route queue: " << text << "\n";
}

inline win_route::Args NrArgs(const Args& a, rq::Op op) {
    win_route::Args r;
    r.remove = op == rq::Op::Remove;
    r.reinstall = op == rq::Op::Reinstall;
    r.install = a.install;
    r.work = a.nr_work;
    r.pid = a.pid;
    r.language = a.language;
    return r;
}
inline fg_route::Args FgArgs(const Args& a, rq::Op op) {
    fg_route::Args r;
    r.remove = op == rq::Op::Remove;
    r.reinstall = op == rq::Op::Reinstall;
    r.install = a.install;
    r.work = a.fg_work;
    r.pid = a.pid;
    r.language = a.language;
    return r;
}

inline std::vector<int> RunOperations(const Args& a, const std::function<void(UpdaterText)>& step) {
    std::vector<int> codes;
    QueueNote(a.install, "start " + a.queue.Serialize());
    step(UpdaterText::Waiting);
    const bool closed = win_route::WaitForGame(a.pid);
    for (const rq::Entry& e : a.queue.Entries()) {
        const std::string name = std::string(rq::RouteName(e.route)) + ":" + rq::OpName(e.op);
        int code = -1;
        try {
            if (e.route == rq::Route::Nr) {
                const win_route::Args nr = NrArgs(a, e.op);
                if (closed) {
                    code = win_route::RouteStep(nr, step);
                } else {
                    win_route::WriteResult(nr, -1, "P.T. was still running after 60 s");
                    step(UpdaterText::RouteFailed);
                    Sleep(3000);
                }
            } else {
                const fg_route::Args fg = FgArgs(a, e.op);
                if (closed) {
                    code = fg_route::FgOp(fg, false, step);
                } else {
                    fg_route::WriteResult(fg, -1, "P.T. was still running after 60 s");
                    step(UpdaterText::FgFailed);
                    Sleep(3000);
                }
            }
        } catch (const std::exception& error) {
            QueueNote(a.install, name + " stopped: " + error.what());
            try {
                if (e.route == rq::Route::Nr) win_route::WriteResult(NrArgs(a, e.op), -1, error.what());
                else fg_route::WriteResult(FgArgs(a, e.op), -1, error.what());
            } catch (const std::exception&) {
            }
            code = -1;
        }
        QueueNote(a.install, name + (code == 0 ? " finished" : " failed (exit " + std::to_string(code) + ")"));
        codes.push_back(code);
    }
    return codes;
}

inline std::optional<Args> ParseQueue(int argc, wchar_t** argv) {
    if (argc < 4 || std::wstring(argv[1]) != L"--route-queue") return std::nullopt;
    Args args;
    args.install = FolderPath(argv[2]);
    std::string ops;
    fs::path nr_work, fg_work;
    for (int i = 3; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--pid" && i + 1 < argc) args.pid = _wcstoui64(argv[++i], nullptr, 10);
        else if (arg == L"--lang" && i + 1 < argc) args.language = std::clamp(_wtoi(argv[++i]), 0, kUpdaterLanguages - 1);
        else if (arg == L"--ops" && i + 1 < argc) {
            const std::wstring wide = argv[++i];
            ops.assign(wide.begin(), wide.end());
        } else if (arg == L"--nr-work" && i + 1 < argc) nr_work = FolderPath(argv[++i]);
        else if (arg == L"--fg-work" && i + 1 < argc) fg_work = FolderPath(argv[++i]);
        else if (arg == L"--") {
            for (++i; i < argc; ++i) args.game_args.emplace_back(argv[i]);
        }
    }
    const fs::path want_nr = (args.install / L"data" / L"dlss5-unlock").lexically_normal(), want_fg = (args.install / L"data" / L"dlssg-unlock").lexically_normal();
    args.nr_work = want_nr;
    args.fg_work = want_fg;
    rq::Queue asked;
    if (!rq::Queue::Parse(ops, asked)) QueueNote(args.install, "the list of operations was not understood: " + ops);
    for (const rq::Entry& e : asked.Entries()) {
        const bool nr = e.route == rq::Route::Nr;
        if ((nr ? nr_work : fg_work).lexically_normal() != (nr ? want_nr : want_fg)) {
            QueueNote(args.install, std::string(rq::RouteName(e.route)) + ": the work folder is not the game's own, left out");
            continue;
        }
        args.queue.Request(e.route, e.op);
    }
    return args;
}

inline int RunQueue(const Args& args) {
    static win_route::State state;
    state.args.install = args.install;
    state.args.language = args.language;
    state.args.game_args = args.game_args;
    state.note = [](const win_route::Args& a, const std::string& text) { QueueNote(a.install, text); };
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
    RECT bounds{0, 0, int(500 * scale), int(85 * scale)};
    AdjustWindowRect(&bounds, style, FALSE);
    const bool has_nr = args.queue.Pending(rq::Route::Nr) != 0, has_fg = args.queue.Pending(rq::Route::Fg) != 0;
    const std::wstring title = has_nr && has_fg ? std::wstring(L"P.T. PC Port: DLSS 5 / DLSS Frame Generation")
                               : win_updater::WideText(UpdaterString(args.language, has_fg ? UpdaterText::FgTitle : UpdaterText::RouteTitle));
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
    state.worker = std::thread([args] {
        RunOperations(args, [](UpdaterText text) { PostMessageW(win_route::g_route->window, win_route::kStep, WPARAM(text), 0); });
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
