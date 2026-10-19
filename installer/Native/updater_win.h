#pragma once
#include <shlobj.h>
#include <shlguid.h>
#include <exdisp.h>
#include <shldisp.h>
#include <commctrl.h>

#include "updater_core.h"
#include "updater_text.h"

namespace pt::setup::win_updater {
namespace fs = std::filesystem;

struct Args {
    fs::path install;
    uint64_t pid = 0;
    int language = 0;
    bool elevated = false;
    std::vector<std::wstring> game_args;
};

inline bool ProcessElevated() {
    HANDLE token = nullptr;
    bool elevated = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) elevated = elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return elevated;
}

inline std::wstring Quote(const std::wstring& arg) { return pt::os::QuoteWindowsArgument(arg); }

inline std::wstring GameCommandLine(const Args& args) {
    std::wstring line;
    for (const auto& arg : args.game_args) line += (line.empty() ? L"" : L" ") + Quote(arg);
    return line;
}

struct Started {
    bool ok = false;
    HANDLE process = nullptr;
};
inline Started StartGame(const Args& args, bool elevated) {
    const fs::path exe = args.install / L"pt.exe";
    const std::wstring parameters = GameCommandLine(args);
    if (!elevated) {
        std::wstring command = Quote(exe.wstring()) + (parameters.empty() ? L"" : L" " + parameters);
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        const wchar_t* folder = args.install.native().size() < MAX_PATH - 2 ? args.install.c_str() : nullptr;
        if (!CreateProcessW(pt::os::ExtendedLengthPath(exe).c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder, &startup, &process)) return {};
        pt::os::AllowForeground(process.dwProcessId);
        CloseHandle(process.hThread);
        return {true, process.hProcess};
    }
    pt::os::AllowForeground(0);
    bool ok = false;
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    IShellWindows* windows = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)))) {
        VARIANT empty{};
        long hwnd = 0;
        IDispatch* dispatch = nullptr;
        if (SUCCEEDED(windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &dispatch)) && dispatch) {
            IServiceProvider* provider = nullptr;
            if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&provider)))) {
                IShellBrowser* browser = nullptr;
                if (SUCCEEDED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) {
                    IShellView* view = nullptr;
                    if (SUCCEEDED(browser->QueryActiveShellView(&view))) {
                        IDispatch* background = nullptr;
                        if (SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)))) {
                            IShellFolderViewDual* folder_view = nullptr;
                            if (SUCCEEDED(background->QueryInterface(IID_PPV_ARGS(&folder_view)))) {
                                IDispatch* application = nullptr;
                                if (SUCCEEDED(folder_view->get_Application(&application))) {
                                    IShellDispatch2* shell = nullptr;
                                    if (SUCCEEDED(application->QueryInterface(IID_PPV_ARGS(&shell)))) {
                                        BSTR file = SysAllocString(exe.c_str());
                                        VARIANT params{}, dir{}, verb{}, show{};
                                        params.vt = VT_BSTR;
                                        params.bstrVal = SysAllocString(parameters.c_str());
                                        dir.vt = VT_BSTR;
                                        dir.bstrVal = SysAllocString(args.install.c_str());
                                        verb.vt = VT_BSTR;
                                        verb.bstrVal = SysAllocString(L"open");
                                        show.vt = VT_I4;
                                        show.lVal = SW_SHOWNORMAL;
#pragma push_macro("ShellExecute")
#undef ShellExecute
                                        ok = SUCCEEDED(shell->ShellExecute(file, params, dir, verb, show));
#pragma pop_macro("ShellExecute")
                                        SysFreeString(file);
                                        VariantClear(&params);
                                        VariantClear(&dir);
                                        VariantClear(&verb);
                                        shell->Release();
                                    }
                                    application->Release();
                                }
                                folder_view->Release();
                            }
                            background->Release();
                        }
                        view->Release();
                    }
                    browser->Release();
                }
                provider->Release();
            }
            dispatch->Release();
        }
        windows->Release();
    }
    if (com) CoUninitialize();
    return {ok, nullptr};
}

inline bool RelaunchElevated(const Args& args, DWORD* error) {
    const std::wstring self = pt::os::ExecutableFile().wstring();
    std::wstring parameters = L"--game-update " + Quote(args.install.wstring()) + L" --pid " + std::to_wstring(args.pid) + L" --lang " +
                              std::to_wstring(args.language) + L" --elevated --";
    for (const auto& arg : args.game_args) parameters += L" " + Quote(arg);
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = self.c_str();
    info.lpParameters = parameters.c_str();
    info.lpDirectory = args.install.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) return true;
    if (error) *error = GetLastError();
    return false;
}

inline std::vector<fs::path> KnownFolders() {
    std::vector<fs::path> folders;
    for (const KNOWNFOLDERID& id : {FOLDERID_Downloads, FOLDERID_Desktop, FOLDERID_Documents}) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) folders.emplace_back(path);
        CoTaskMemFree(path);
    }
    return folders;
}

struct PackageView {
    HANDLE lock = INVALID_HANDLE_VALUE;
    HMODULE module = nullptr;
    void Close() {
        if (module) FreeLibrary(module);
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
        module = nullptr;
        lock = INVALID_HANDLE_VALUE;
    }
    ~PackageView() { Close(); }
};
inline std::pair<const unsigned char*, size_t> OpenPayload(PackageView& view, const fs::path& package) {
    view.lock = CreateFileW(package.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (view.lock == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open the downloaded package.");
    view.module = LoadLibraryExW(package.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!view.module) throw std::runtime_error("The downloaded package is not a setup program.");
    HRSRC resource = FindResourceW(view.module, MAKEINTRESOURCEW(100), RT_RCDATA);
    if (!resource) throw std::runtime_error("The downloaded package holds no payload.");
    HGLOBAL loaded = LoadResource(view.module, resource);
    const auto* data = static_cast<const unsigned char*>(LockResource(loaded));
    const size_t size = SizeofResource(view.module, resource);
    if (!data || size < 12 || std::memcmp(data, "PTSETUP1", 8) != 0) throw std::runtime_error("The downloaded package holds no payload.");
    return {data, size};
}

constexpr UINT kPhase = WM_APP + 11, kPercent = WM_APP + 12, kDone = WM_APP + 13;
struct Outcome {
    enum Kind { Updated, Failed, RolledBack, Broken, Elevated, Unconfirmed } kind = Failed;
    std::string detail;
    std::string page;
    bool start_old_game = false;
};
struct State {
    Args args;
    HWND window = nullptr, status = nullptr, bar = nullptr, cancel = nullptr;
    HFONT font = nullptr;
    std::atomic<bool> cancelled{false};
    std::atomic<int> last_permille{-1};
    std::string version;
    UpdatePhase phase = UpdatePhase::Waiting;
    int percent = -1;
    bool quiet = false;
    fs::path result_file;
    std::thread worker;
};
inline State* g_state = nullptr;

inline std::wstring PhaseText(const State& s) {
    const UpdaterText key = s.phase == UpdatePhase::Waiting      ? UpdaterText::Waiting
                            : s.phase == UpdatePhase::Checking    ? UpdaterText::Checking
                            : s.phase == UpdatePhase::Downloading ? UpdaterText::Downloading
                            : s.phase == UpdatePhase::Verifying   ? UpdaterText::Verifying
                            : s.phase == UpdatePhase::Installing  ? UpdaterText::Installing
                                                                  : UpdaterText::Starting;
    std::string text = UpdaterString(s.args.language, key, s.version);
    if (s.percent >= 0 && (s.phase == UpdatePhase::Downloading || s.phase == UpdatePhase::Installing)) text += " " + std::to_string(s.percent) + "%";
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring wide(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), n);
    return wide;
}
inline std::wstring WideText(const std::string& text) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring wide(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), n);
    return wide;
}

inline void Note(const State& s, const std::string& line) {
    std::error_code error;
    if (!fs::is_directory(s.args.install / L"data", error)) return;
    std::ofstream log(s.args.install / L"data" / L"pt-updater.log", std::ios::app | std::ios::binary);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    log << stamp << line << "\n";
}

inline Outcome Work(State& s) {
    Outcome outcome;
    auto post_phase = [&](UpdatePhase phase) {
        if (s.window) PostMessageW(s.window, kPhase, WPARAM(phase), 0);
        Note(s, "phase " + std::to_string(int(phase)));
    };
    try {
        post_phase(UpdatePhase::Waiting);
        if (s.args.pid) {
            if (HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, DWORD(s.args.pid))) {
                const DWORD waited = WaitForSingleObject(game, 60000);
                CloseHandle(game);
                if (waited == WAIT_TIMEOUT) throw std::runtime_error("P.T. is still running.");
            }
        }
        bool writable = true;
        try {
            ProbeWritableDirectory(s.args.install, "updater-" + std::to_string(GetCurrentProcessId()), "game folder");
            ProbeWritableDirectory(s.args.install.parent_path(), "updater-" + std::to_string(GetCurrentProcessId()), "folder above the game");
        } catch (const std::exception&) {
            writable = false;
        }
        if (!writable && !s.args.elevated) {
            if (const char* refuse = std::getenv("PT_UPDATE_TEST_NO_ELEVATE"); refuse && update::TestOverrides()) {
                throw std::runtime_error("the game folder is not writable; elevation would be asked for (test)");
            }
            if (s.window) PostMessageW(s.window, kPhase, WPARAM(UpdatePhase::Waiting), 1);
            DWORD error = 0;
            if (RelaunchElevated(s.args, &error)) {
                outcome.kind = Outcome::Elevated;
                Note(s, "relaunched elevated");
                return outcome;
            }
            throw std::runtime_error(error == ERROR_CANCELLED ? "administrator rights were not given" : "could not ask for administrator rights");
        }
        GameUpdatePlatform platform;
        GUID guid{};
        CoCreateGuid(&guid);
        wchar_t id[40];
        StringFromGUID2(guid, id, 40);
        const std::wstring wide(id);
        platform.unique_id = std::string(wide.begin() + 1, wide.end() - 1);
        platform.pid = GetCurrentProcessId();
        platform.check_parents = CheckParents;
        platform.hash_file = Hash;
        platform.hash_package = HashBody;
        auto view = std::make_shared<PackageView>();
        platform.open_payload = [view](const fs::path& package) { return OpenPayload(*view, package); };
        platform.close_payload = [view] { view->Close(); };
        platform.validate_runtime = [](const std::vector<InstalledFile>& files) {
            RequireProgramFiles(files, {"amd_fidelityfx_vk.dll", "nvngx_dlss.dll", "libxess.dll"});
        };
        platform.process_alive = [](uint64_t pid) { return pt::os::ProcessAlive(pid); };
        platform.known_folders = KnownFolders;
        platform.phase = [&](UpdatePhase phase) {
            if (phase == UpdatePhase::Installing && s.window) PostMessageW(s.window, kPercent, WPARAM(-1), 0);
            post_phase(phase);
        };
        if (update::TestOverrides())
            if (const char* fail = std::getenv("PT_UPDATE_TEST_FAIL_AT")) platform.fail_at = std::atoi(fail);
        std::string page;
        GameUpdateResult result;
        try {
            result = ApplyGameUpdate(s.args.install, platform, page);
        } catch (const std::exception& e) {
            view.reset();
            outcome.page = page;
            outcome.detail = e.what();
            std::error_code error;
            outcome.kind = fs::exists(JournalFile(s.args.install), error) && update_journal::Read(JournalFile(s.args.install)) ? Outcome::Broken : Outcome::Failed;
            outcome.start_old_game = outcome.kind == Outcome::Failed;
            Note(s, std::string("failed: ") + e.what());
            return outcome;
        }
        view.reset();
        outcome.page = page;
        s.version = result.version;
        Note(s, "installed " + result.version + ": " + std::to_string(result.outcome.swap.replaced) + " replaced, " + std::to_string(result.outcome.swap.added) +
                    " added, " + std::to_string(result.outcome.unchanged) + " unchanged, " + std::to_string(result.outcome.swap.removed) + " removed, " +
                    std::to_string(result.outcome.verified) + " checked, " + std::to_string(result.outcome.repaired) + " fetched again; downloaded " +
                    std::to_string(result.downloaded) + " bytes in " + std::to_string(result.requests) + " requests (" +
                    (result.ranged ? "parts of the package" : "the whole package") + ")" +
                    (result.outcome.title_picture_missing ? ", title picture still missing" : ""));
        post_phase(UpdatePhase::Starting);
        const fs::path journal = result.outcome.journal;
        const Started game = StartGame(s.args, ProcessElevated());
        if (!game.ok) {
            outcome.detail = "the new version could not be started";
        }
        std::function<std::optional<int>()> exit_code;
        if (game.process)
            exit_code = [process = game.process]() -> std::optional<int> {
                DWORD code = 0;
                if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0 || !GetExitCodeProcess(process, &code)) return std::nullopt;
                return int(code);
            };
        else if (!game.ok)
            exit_code = []() -> std::optional<int> { return 1; };
        int code = 0;
        const StartResult started = WaitForStart(journal, exit_code, std::chrono::seconds(120), &code);
        if (game.process) CloseHandle(game.process);
        if (started == StartResult::Started) {
            const bool all = FinishGameUpdate(journal);
            outcome.kind = Outcome::Updated;
            Note(s, all ? "the new version started; backup removed" : "the new version started; backup removed except files in use (the game's next start removes them)");
            return outcome;
        }
        if (started == StartResult::Unconfirmed) {
            outcome.kind = Outcome::Unconfirmed;
            Note(s, "the new version's start was not confirmed; the backup stays until it starts");
            return outcome;
        }
        std::string why;
        if (outcome.detail.empty()) outcome.detail = "the new version stopped at its start (exit code " + std::to_string(code) + ")";
        if (RollBackGameUpdate(journal, &why)) {
            outcome.kind = Outcome::RolledBack;
            outcome.start_old_game = true;
            Note(s, "rolled back: " + outcome.detail);
        } else {
            outcome.kind = Outcome::Broken;
            outcome.detail += "; " + why;
            Note(s, "rollback failed: " + why);
        }
        return outcome;
    } catch (const std::exception& e) {
        outcome.kind = Outcome::Failed;
        outcome.detail = e.what();
        outcome.start_old_game = true;
        Note(s, std::string("failed: ") + e.what());
        return outcome;
    }
}

inline void ShowOutcome(State& s, const Outcome& outcome) {
    const int lang = s.args.language;
    const UpdaterText main = outcome.kind == Outcome::RolledBack ? UpdaterText::RolledBack
                             : outcome.kind == Outcome::Broken   ? UpdaterText::Broken
                                                                 : UpdaterText::Failed;
    std::string content = UpdaterString(lang, UpdaterText::Fallback);
    if (!outcome.page.empty()) content += "\n" + outcome.page;
    content += "\n\n" + UpdaterString(lang, UpdaterText::Details) + " " + outcome.detail;
    const std::wstring title = WideText(UpdaterString(lang, UpdaterText::Title)), instruction = WideText(UpdaterString(lang, main)),
                       body = WideText(content), open = WideText(UpdaterString(lang, UpdaterText::OpenPage)),
                       close = WideText(UpdaterString(lang, UpdaterText::Close));
    TASKDIALOG_BUTTON buttons[] = {{100, open.c_str()}, {IDCLOSE, close.c_str()}};
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = s.window;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | (lang == 9 ? TDF_RTL_LAYOUT : 0);
    config.pszWindowTitle = title.c_str();
    config.pszMainIcon = TD_ERROR_ICON;
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = body.c_str();
    config.cButtons = outcome.page.empty() ? 0 : 2;
    config.pButtons = buttons;
    config.dwCommonButtons = outcome.page.empty() ? TDCBF_CLOSE_BUTTON : 0;
    config.nDefaultButton = outcome.page.empty() ? IDCLOSE : 100;
    int pressed = 0;
    if (SUCCEEDED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr)) && pressed == 100 && outcome.page.starts_with("https://"))
        ShellExecuteW(nullptr, L"open", WideText(outcome.page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

inline void Finish(State& s, const Outcome& outcome) {
    if (s.quiet) {
        std::ofstream out(s.result_file, std::ios::binary | std::ios::trunc);
        static const char* const kinds[] = {"PASS updated", "FAIL failed", "FAIL rolled-back", "FAIL broken", "PASS elevated", "PASS unconfirmed"};
        out << kinds[outcome.kind] << " " << s.version << "\npage " << outcome.page << "\ndetail " << outcome.detail << "\n";
    } else if (outcome.kind == Outcome::Failed || outcome.kind == Outcome::RolledBack || outcome.kind == Outcome::Broken) {
        ShowOutcome(s, outcome);
    }
    if (outcome.start_old_game && !s.quiet) {
        const Started game = StartGame(s.args, ProcessElevated());
        if (game.process) CloseHandle(game.process);
    }
}

inline LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    State* s = g_state;
    if (message == WM_CTLCOLORSTATIC) {
        SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    if (message == kPhase && s) {
        s->phase = UpdatePhase(wp);
        s->percent = -1;
        std::wstring text = lp == 1 ? WideText(UpdaterString(s->args.language, UpdaterText::Elevating)) : PhaseText(*s);
        SetWindowTextW(s->status, text.c_str());
        const bool measured = s->phase == UpdatePhase::Downloading || s->phase == UpdatePhase::Installing;
        LONG_PTR style = GetWindowLongPtrW(s->bar, GWL_STYLE);
        if (measured) {
            SendMessageW(s->bar, PBM_SETMARQUEE, FALSE, 0);
            SetWindowLongPtrW(s->bar, GWL_STYLE, style & ~LONG_PTR(PBS_MARQUEE));
            SendMessageW(s->bar, PBM_SETRANGE32, 0, 1000);
            SendMessageW(s->bar, PBM_SETPOS, 0, 0);
        } else {
            SetWindowLongPtrW(s->bar, GWL_STYLE, style | PBS_MARQUEE);
            SendMessageW(s->bar, PBM_SETMARQUEE, TRUE, 30);
        }
        EnableWindow(s->cancel, s->phase == UpdatePhase::Downloading || s->phase == UpdatePhase::Checking || s->phase == UpdatePhase::Waiting);
        return 0;
    }
    if (message == kPercent && s) {
        const int permille = int(wp);
        if (permille < 0) return 0;
        SendMessageW(s->bar, PBM_SETPOS, permille + 1, 0);
        SendMessageW(s->bar, PBM_SETPOS, permille, 0);
        s->percent = permille / 10;
        SetWindowTextW(s->status, PhaseText(*s).c_str());
        return 0;
    }
    if (message == kDone && s) {
        auto* outcome = reinterpret_cast<Outcome*>(lp);
        if (s->worker.joinable()) s->worker.join();
        ShowWindow(hwnd, SW_HIDE);
        Finish(*s, *outcome);
        delete outcome;
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wp) == 2 && s) {
        s->cancelled = true;
        return 0;
    }
    if (message == WM_CLOSE && s) {
        s->cancelled = true;
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

inline int Run(const Args& args) {
    static State state;
    state.args = args;
    g_state = &state;
    if (update::TestOverrides()) {
        if (const std::string result = pt::os::GetEnv("PT_UPDATE_TEST_RESULT"); !result.empty()) {
            state.quiet = true;
            state.result_file = pt::os::PathFromUtf8(result);
        }
    }
    hooks.cancelled = [] { return g_state && g_state->cancelled.load(); };
    hooks.report = [](const std::string&) {};
    hooks.progress = [](uint64_t done, uint64_t total) {
        if (!g_state || !g_state->window || !total) return;
        const int permille = int(done * 1000 / total);
        if (permille != g_state->last_permille.exchange(permille)) PostMessageW(g_state->window, kPercent, WPARAM(permille), 0);
    };
    Note(state, "updater started for " + std::string(update::CurrentVersion()) + (args.elevated ? " (elevated)" : ""));
    if (state.quiet) {
        const Outcome outcome = Work(state);
        Finish(state, outcome);
        return outcome.kind == Outcome::Updated || outcome.kind == Outcome::Elevated ? 0 : 1;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&common);
    const float scale = GetDpiForSystem() / 96.0f;
    state.font = CreateFontW(int(-15 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW cls{};
    cls.lpfnWndProc = Proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"PTUpdater";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    RegisterClassW(&cls);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, int(460 * scale), int(130 * scale)};
    AdjustWindowRect(&bounds, style, FALSE);
    const std::wstring title = WideText(UpdaterString(args.language, UpdaterText::Title));
    state.window = CreateWindowExW(args.language == 9 ? WS_EX_LAYOUTRTL : 0, cls.lpszClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                   bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, nullptr);
    auto control = [&](const wchar_t* kind, const wchar_t* text, DWORD extra, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | extra, int(x * scale), int(y * scale), int(w * scale), int(h * scale),
                                 state.window, reinterpret_cast<HMENU>(INT_PTR(id)), cls.hInstance, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        return c;
    };
    state.status = control(L"STATIC", PhaseText(state).c_str(), 0, 20, 18, 420, 24, 0);
    state.bar = control(PROGRESS_CLASSW, L"", PBS_MARQUEE, 20, 50, 420, 15, 0);
    SendMessageW(state.bar, PBM_SETMARQUEE, TRUE, 30);
    state.cancel = control(L"BUTTON", WideText(UpdaterString(args.language, UpdaterText::Cancel)).c_str(), WS_TABSTOP, 340, 84, 100, 30, 2);
    ShowWindow(state.window, SW_SHOWNORMAL);
    state.worker = std::thread([] {
        auto* outcome = new Outcome(Work(*g_state));
        PostMessageW(g_state->window, kDone, 0, reinterpret_cast<LPARAM>(outcome));
    });
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(state.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (state.worker.joinable()) state.worker.join();
    DeleteObject(state.font);
    CoUninitialize();
    return 0;
}

inline std::optional<Args> Parse(int argc, wchar_t** argv) {
    if (argc < 3 || std::wstring(argv[1]) != L"--game-update") return std::nullopt;
    Args args;
    args.install = FolderPath(argv[2]);
    for (int i = 3; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--pid" && i + 1 < argc) args.pid = _wcstoui64(argv[++i], nullptr, 10);
        else if (arg == L"--lang" && i + 1 < argc) args.language = std::clamp(_wtoi(argv[++i]), 0, kUpdaterLanguages - 1);
        else if (arg == L"--elevated") args.elevated = true;
        else if (arg == L"--") {
            for (++i; i < argc; ++i) args.game_args.emplace_back(argv[i]);
        }
    }
    return args;
}

}
