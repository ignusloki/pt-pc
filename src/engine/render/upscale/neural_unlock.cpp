#include "engine/render/upscale/neural_unlock.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>

#include "engine/core/ascii.h"
#include "engine/core/log.h"
#include "engine/platform/http.h"
#include "engine/platform/json_value.h"
#include "engine/platform/os.h"
#include "engine/platform/self_integrity.h"
#include "engine/platform/update_check.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace pt::nr_unlock {

namespace {

constexpr uint32_t kNvidia = 0x10DE;

std::string ReadText(const std::filesystem::path& path, size_t max_bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) return {};
    const std::streamoff start = size > static_cast<std::streamoff>(max_bytes) ? size - static_cast<std::streamoff>(max_bytes) : 0;
    in.seekg(start);
    std::string text(static_cast<size_t>(size - start), '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(in.gcount()));
    return text;
}

const metadata_json::Value* Member(const metadata_json::Value& v, const char* name) {
    const auto it = v.members.find(name);
    return it == v.members.end() ? nullptr : &it->second;
}

std::string MemberText(const metadata_json::Value& v, const char* name) {
    const metadata_json::Value* m = Member(v, name);
    return m && !m->object && m->items.empty() ? m->text : std::string();
}

std::string Hex(const unsigned char* digest) {
    static const char* hex = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    return out;
}

std::string_view SectionOf(std::string_view line) {
    size_t a = 0;
    while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
    size_t b = line.size();
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) --b;
    if (b - a >= 2 && line[a] == '[' && line[b - 1] == ']') return line.substr(a + 1, b - a - 2);
    return {};
}

std::string KeyOf(std::string_view line) {
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) return {};
    std::string_view key = line.substr(0, eq);
    while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.remove_prefix(1);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.remove_suffix(1);
    return AsciiLowerCopy(key);
}

std::string HexCodeAfter(std::string_view line, std::string_view marker) {
    const size_t at = line.find(marker);
    if (at == std::string_view::npos) return {};
    size_t i = at + marker.size();
    std::string code = "0x";
    while (i < line.size() && std::isxdigit(static_cast<unsigned char>(line[i]))) code.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(line[i++]))));
    return code.size() > 2 ? code : std::string();
}

std::string EnvOr(const char* name, std::string_view fallback) {
    const std::string value = os::GetEnv(name);
    return value.empty() ? std::string(fallback) : value;
}

std::string FileSha256Impl(const std::filesystem::path& path, uint64_t* size_out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (size_out) *size_out = bytes.size();
    unsigned char digest[32];
    if (!integrity::Sha256(bytes.data(), bytes.size(), digest)) return {};
    return Hex(digest);
}

std::filesystem::path FileUrlPath(const std::string& url) {
    return url.compare(0, 5, "file:") == 0 ? os::PathFromUtf8(url.substr(5)) : std::filesystem::path();
}

std::optional<std::string> HttpGetText(const std::string& url, bool metadata) {
    if (const std::filesystem::path f = FileUrlPath(url); !f.empty()) {
        const std::string text = ReadText(f, 256 * 1024);
        return std::filesystem::exists(f) ? std::optional<std::string>(text) : std::nullopt;
    }
    if (!(metadata ? update::AllowedMetadataUrl(url) : update::AllowedAssetUrl(url))) return std::nullopt;
    http::Request request;
    request.url = url;
    request.timeout_ms = 8000;
    request.follow_redirects = true;
    request.max_body = 256 * 1024;
    const auto response = http::Get(request);
    if (!response || response->status != 200) return std::nullopt;
    return response->body;
}

std::string ShaFromSums(std::string_view sums, std::string_view suffix) {
    std::string_view rest = sums;
    while (!rest.empty()) {
        const size_t nl = rest.find('\n');
        std::string_view line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
        const size_t sp = line.find_first_of(" \t");
        if (sp == 64) {
            std::string_view name = line.substr(sp);
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t' || name.front() == '*')) name.remove_prefix(1);
            while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.remove_suffix(1);
            const std::string norm(name);
            std::string low = AsciiLowerCopy(norm);
            std::replace(low.begin(), low.end(), '\\', '/');
            std::string suf = AsciiLowerCopy(std::string(suffix));
            std::replace(suf.begin(), suf.end(), '\\', '/');
            if (low.size() >= suf.size() && low.compare(low.size() - suf.size(), suf.size(), suf) == 0) {
                return AsciiLowerCopy(std::string(line.substr(0, 64)));
            }
        }
    }
    return {};
}

std::string AssetUrl(const metadata_json::Value& release, std::string_view suffix) {
    const auto it = release.members.find("assets");
    if (it == release.members.end()) return {};
    for (const metadata_json::Value& a : it->second.items) {
        const std::string name = MemberText(a, "name");
        const std::string low = AsciiLowerCopy(name);
        if (low.size() >= suffix.size() && low.compare(low.size() - suffix.size(), suffix.size(), AsciiLowerCopy(std::string(suffix))) == 0) {
            return MemberText(a, "browser_download_url");
        }
    }
    return {};
}

std::string Download(const std::string& url, const std::filesystem::path& dest, uint64_t max_bytes, const std::function<void(int)>& percent,
                     const std::function<bool()>& cancelled, std::string* error) {
    (void)max_bytes;
    if (const std::filesystem::path f = FileUrlPath(url); !f.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(f, ec)) {
            if (error) *error = "404 (fixture not found)";
            return {};
        }
        std::filesystem::remove(dest, ec);
        std::filesystem::copy_file(f, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            if (error) *error = "cannot copy the fixture: " + ec.message();
            return {};
        }
        if (percent) percent(100);
        return FileSha256Impl(dest, nullptr);
    }
    const std::filesystem::path part = dest.string() + ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + os::PathToUtf8(part);
            return {};
        }
        http::FetchRequest request;
        request.url = url;
        request.max_bytes = max_bytes;
        request.allow = [](const std::string& u) { return update::AllowedAssetUrl(u); };
        request.write = [&](const char* data, size_t size) {
            out.write(data, static_cast<std::streamsize>(size));
            return static_cast<bool>(out);
        };
        request.progress = [&](uint64_t done, uint64_t total) { if (percent) percent(static_cast<int>(done * 100 / std::max<uint64_t>(total ? total : max_bytes, 1))); };
        request.cancelled = [&] { return cancelled && cancelled(); };
        const http::FetchResult r = http::Fetch(request);
        if (!r.ok) {
            if (error) *error = std::format("{} (HTTP {})", r.error.empty() ? "no answer" : r.error, r.status);
            std::error_code ec;
            std::filesystem::remove(part, ec);
            return {};
        }
    }
    std::error_code ec;
    std::filesystem::rename(part, dest, ec);
    if (ec) {
        if (error) *error = "cannot rename the download: " + ec.message();
        std::filesystem::remove(part, ec);
        return {};
    }
    return FileSha256Impl(dest, nullptr);
}

}

GpuClass ClassifyGpu(uint32_t vendor_id, uint32_t device_id, std::string_view adapter) {
    if (vendor_id != kNvidia) return GpuClass::Other;
    const std::string name = AsciiLowerCopy(adapter);
    const size_t rtx = name.find("rtx");
    if (rtx == std::string::npos) return GpuClass::Other;
    if (device_id != 0) {
        if (device_id >= 0x2900) return GpuClass::Native;
        if (device_id >= 0x1E00) return GpuClass::NeedsUnlock;
        return GpuClass::Other;
    }
    size_t i = rtx + 3;
    while (i < name.size() && !std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    if (i + 1 < name.size()) {
        const int series = (name[i] - '0') * 10 + (name[i + 1] - '0');
        if (series >= 50 && series < 100) return GpuClass::Native;
        if (series == 20 || series == 30 || series == 40) return GpuClass::NeedsUnlock;
    }
    return GpuClass::Other;
}

std::optional<Install> ParseInstall(std::string_view text) {
    metadata_json::Value root;
    if (!metadata_json::Reader(text).Parse(root) || !root.object) return std::nullopt;
    Install install;
    install.complete = MemberText(root, "complete") == "true";
    install.route = MemberText(root, "path");
    install.tool = MemberText(root, "tool");
    if (const metadata_json::Value* components = Member(root, "components"); components && components->object) {
        install.reshade = MemberText(*components, "reshade");
        install.addon = MemberText(*components, "renodx");
        install.runtime = MemberText(*components, "dlssnr");
    }
    if (const metadata_json::Value* files = Member(root, "files")) {
        for (const metadata_json::Value& f : files->items) {
            if (f.is_string && !f.text.empty()) install.files.push_back(f.text);
        }
    }
    return install;
}

std::optional<Install> ReadInstall(const std::filesystem::path& game_dir) {
    std::error_code ec;
    const std::filesystem::path manifest = game_dir / kManifest;
    if (!std::filesystem::is_regular_file(manifest, ec)) return std::nullopt;
    return ParseInstall(ReadText(manifest, 1 << 20));
}

bool Usable(const Install& install) {
    if (!install.complete || install.route != kRoute) return false;
    return std::any_of(install.files.begin(), install.files.end(),
                       [](const std::string& f) { return AsciiLowerCopy(f) == "renodx-dlss5.addon64"; });
}

void SetSectionKeys(std::vector<std::string>& lines, std::string_view section, const std::vector<std::pair<std::string, std::string>>& wanted) {
    const std::string section_lower = AsciiLowerCopy(std::string(section));
    size_t begin = lines.size(), end = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view name = SectionOf(lines[i]);
        if (name.empty()) continue;
        if (begin == lines.size() && AsciiLowerCopy(std::string(name)) == section_lower) {
            begin = i;
        } else if (begin != lines.size()) {
            end = i;
            break;
        }
    }
    std::vector<bool> done(wanted.size(), false);
    if (begin != lines.size()) {
        for (size_t i = begin + 1; i < end; ++i) {
            const std::string key = KeyOf(lines[i]);
            for (size_t w = 0; w < wanted.size(); ++w) {
                if (key == AsciiLowerCopy(wanted[w].first)) {
                    lines[i] = wanted[w].first + "=" + wanted[w].second;
                    done[w] = true;
                }
            }
        }
        size_t at = end;
        while (at > begin + 1 && lines[at - 1].empty()) --at;
        std::vector<std::string> add;
        for (size_t w = 0; w < wanted.size(); ++w) {
            if (!done[w]) add.push_back(wanted[w].first + "=" + wanted[w].second);
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), add.begin(), add.end());
    } else {
        while (!lines.empty() && lines.back().empty()) lines.pop_back();
        if (!lines.empty()) lines.emplace_back();
        lines.push_back("[" + std::string(section) + "]");
        for (const auto& [key, value] : wanted) lines.push_back(key + "=" + value);
    }
}

std::string ApplyAddonSettings(std::string_view ini_text, const AddonSettings& settings) {
    const std::string eol = ini_text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    std::vector<std::pair<std::string, std::string>> wanted = {
        {"NeuralUplift", settings.on ? "1" : "0"},
        {"NRStyle", std::to_string(std::clamp(settings.style, 0, 2))},
        {"NRIntensity", std::format("{:.2f}", std::clamp(settings.intensity, 0.0f, 1.0f))},
        {"NRPaperWhiteScale", std::format("{:.1f}", kPaperWhite)},
        {"NRLocalTone", std::format("{:.1f}", kLocalTone)},
        {"NRColorStrength", "1.0"},
        {"NRTransferStrength", "1.0"},
        {"NRPreset", "0"},
        {"NREnableUpscaling", "0"},
        {"NRHookPoint", "1"},
        {"NRPasses", "1"},
        {"NRUICorrection", "0"},
        {"EnableHooks", "2"},
        {"NRToggleKey", std::to_string(kNrToggleVk)},
        {"NRScreenshotKey", std::to_string(kNrScreenshotVk)},
    };
    for (const auto& [key, value] : settings.extra) {
        auto it = std::find_if(wanted.begin(), wanted.end(), [&](const auto& w) { return AsciiLowerCopy(w.first) == AsciiLowerCopy(key); });
        if (it != wanted.end()) it->second = value;
        else wanted.emplace_back(key, value);
    }
    std::vector<std::string> lines;
    {
        std::string_view rest = ini_text;
        while (!rest.empty()) {
            const size_t nl = rest.find('\n');
            std::string_view line = rest.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            lines.emplace_back(line);
            if (nl == std::string_view::npos) break;
            rest.remove_prefix(nl + 1);
        }
    }
    SetSectionKeys(lines, kAddonSection, wanted);
    SetSectionKeys(lines, "ADDON", {{"AddonPath", settings.parked ? std::string(kParkedAddonPath) : std::string(".\\")}});
    SetSectionKeys(lines, "GENERAL", {{"EffectSearchPaths", ""}, {"TextureSearchPaths", ""}, {"PerformanceMode", "1"}, {"NoReloadOnInit", "1"},
                                      {"NoEffectCache", "1"}, {"PresetPath", ""}, {"StartupPresetPath", ""}, {"PresetShortcutKeys", ""},
                                      {"PresetShortcutPaths", ""}});
    SetSectionKeys(lines, "INPUT", {{"KeyOverlay", "0,0,0,0"}, {"KeyEffects", "0,0,0,0"}, {"KeyReload", "0,0,0,0"}, {"KeyScreenshot", "0,0,0,0"},
                                    {"KeyNextPreset", "0,0,0,0"}, {"KeyPreviousPreset", "0,0,0,0"}, {"KeyFPS", "0,0,0,0"},
                                    {"KeyFrameTime", "0,0,0,0"}, {"GamepadNavigation", "0"}});
    SetSectionKeys(lines, "OVERLAY", {{"TutorialProgress", "4"}, {"ShowFPS", "0"}, {"ShowClock", "0"}, {"ShowFrameTime", "0"},
                                      {"ShowPresetName", "0"}, {"ShowScreenshotMessage", "0"}, {"ShowPresetTransitionMessage", "0"},
                                      {"ShowForceLoadEffectsButton", "0"}});
    SetSectionKeys(lines, "SCREENSHOT", {{"SavePath", "data/reshade-screenshots"}});
    std::string out;
    for (const std::string& line : lines) out += line + eol;
    return out;
}

std::vector<std::pair<std::string, std::string>> ParseAddonKeys(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> out;
    while (!text.empty()) {
        const size_t end = text.find(';');
        const std::string_view item = text.substr(0, end);
        const size_t eq = item.find('=');
        if (eq != std::string_view::npos && eq > 0) out.emplace_back(std::string(item.substr(0, eq)), std::string(item.substr(eq + 1)));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return out;
}

bool WriteAddonSettings(const std::filesystem::path& game_dir, const AddonSettings& settings, std::string* error) {
    const std::filesystem::path path = game_dir / "ReShade.ini";
    const std::string before = ReadText(path, 4 << 20);
    const std::string after = ApplyAddonSettings(before, settings);
    if (after == before) return true;
    const std::filesystem::path temp = game_dir / "ReShade.ini.pt-tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << after;
        if (!out) {
            if (error) *error = "cannot write " + os::PathToUtf8(temp);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        if (error) *error = "cannot replace " + os::PathToUtf8(path);
        return false;
    }
    return true;
}

bool IsOwnReShade(const std::filesystem::path& game_dir) {
    std::error_code ec;
    const std::filesystem::path own = game_dir / kOwnReShadeDir / kOwnReShadeFile, put = game_dir / kOwnReShadeFile;
    if (!std::filesystem::is_regular_file(own, ec) || !std::filesystem::is_regular_file(put, ec)) return false;
    if (std::filesystem::file_size(own, ec) != std::filesystem::file_size(put, ec)) return false;
    const std::string a = FileSha256(own);
    return !a.empty() && a == FileSha256(put);
}

RouteReport ParseReShadeLog(std::string_view text) {
    RouteReport report;
    bool failed = false;
    std::string_view rest = text;
    while (!rest.empty()) {
        const size_t nl = rest.find('\n');
        std::string_view line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
        if (line.find("DLSS 5") == std::string_view::npos && line.find("DLSS5") == std::string_view::npos) continue;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        std::string code;
        const bool failure = !(code = HexCodeAfter(line, "feature 18 create failed with 0x")).empty() ||
                             !(code = HexCodeAfter(line, "feature 18 evaluate failed with 0x")).empty() ||
                             !(code = HexCodeAfter(line, "direct Init_Ext failed with 0x")).empty() ||
                             line.find("is not a usable signed NGX runtime") != std::string_view::npos ||
                             line.find("not a valid signed NGX runtime") != std::string_view::npos ||
                             line.find("NR is unavailable in this session") != std::string_view::npos ||
                             line.find("nvngx_dlssnr.dll was not found") != std::string_view::npos;
        if (failure) {
            failed = true;
            report.state = RouteReport::State::Failed;
            report.code = code;
            report.line = std::string(line);
        } else if (!failed && line.find("feature 18 evaluation succeeded") != std::string_view::npos) {
            report.state = RouteReport::State::Running;
            report.line = std::string(line);
        } else if (report.state == RouteReport::State::Unknown && line.find("NR is currently OFF") != std::string_view::npos) {
            report.state = RouteReport::State::Off;
            report.line = std::string(line);
        }
    }
    return report;
}

RouteReport ReadRouteReport(const std::filesystem::path& game_dir) {
    return ParseReShadeLog(ReadText(game_dir / "ReShade.log", 2 << 20));
}

bool SameFolder(const std::filesystem::path& a, const std::filesystem::path& b) {
    auto key = [](const std::filesystem::path& p) {
        std::string text = os::PathToUtf8(p.lexically_normal());
        while (text.size() > 1 && (text.back() == '\\' || text.back() == '/')) text.pop_back();
        for (char& c : text) {
            if (c == '\\') c = '/';
        }
#ifdef _WIN32
        text = AsciiLowerCopy(text);
#endif
        return text;
    };
    return !a.empty() && !b.empty() && key(a) == key(b);
}

#ifdef _WIN32
namespace {
std::string ForeignModules(const std::filesystem::path& game_dir) {
    HMODULE modules[512] = {};
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) return {};
    wchar_t windows_dir[MAX_PATH] = {};
    GetWindowsDirectoryW(windows_dir, MAX_PATH);
    const std::string windows = AsciiLowerCopy(os::PathToUtf8(std::filesystem::path(windows_dir)));
    std::string out;
    const size_t count = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));
    for (size_t i = 0; i < count; ++i) {
        wchar_t name[MAX_PATH * 2] = {};
        if (!GetModuleFileNameW(modules[i], name, MAX_PATH * 2)) continue;
        const std::filesystem::path path(name);
        const std::string lower = AsciiLowerCopy(os::PathToUtf8(path));
        if (!windows.empty() && lower.starts_with(windows)) continue;
        if (SameFolder(path.parent_path(), game_dir)) continue;
        out += out.empty() ? "; modules from outside Windows and the game folder: " : ", ";
        out += os::PathToUtf8(path);
    }
    return out;
}
}
#endif

bool LoadDxgiForRoute(const std::filesystem::path& game_dir, bool install_present, bool load) {
#ifdef _WIN32
    if (!install_present) return false;
    if (HMODULE loaded = GetModuleHandleW(L"dxgi.dll")) {
        wchar_t name[MAX_PATH * 2] = {};
        GetModuleFileNameW(loaded, name, MAX_PATH * 2);
        const bool reshade = SameFolder(std::filesystem::path(name).parent_path(), game_dir);
        LogWarn("neural rendering: dxgi.dll was already loaded before the DLSS 5 route's choice ({}{}){}", os::PathToUtf8(name),
                reshade ? ", the route's ReShade" : "", ForeignModules(game_dir));
        return reshade;
    }
    if (load) {
        const std::filesystem::path reshade = game_dir / "dxgi.dll";
        std::error_code ec;
        if (std::filesystem::is_regular_file(reshade, ec)) {
            if (HMODULE wininet = LoadLibraryW(L"wininet.dll")) {
                using SetOption = BOOL(WINAPI*)(void*, DWORD, void*, DWORD);
                if (auto set = reinterpret_cast<SetOption>(reinterpret_cast<void*>(GetProcAddress(wininet, "InternetSetOptionA")))) {
                    struct {
                        DWORD access;
                        const char* proxy;
                        const char* bypass;
                    } info{3 /* INTERNET_OPEN_TYPE_PROXY */, "0.0.0.0:9", "<-loopback>"};
                    const BOOL ok = set(nullptr, 38 /* INTERNET_OPTION_PROXY */, &info, sizeof(info));
                    LogInfo("neural rendering: ReShade's update check kept off the network ({})", ok ? "a WinINet proxy for this process" : "could not be set");
                }
            }
            const bool own = IsOwnReShade(game_dir);
            if (LoadLibraryExW(reshade.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
                LogInfo("neural rendering: the community route's ReShade loaded from {} ({}; DLSS 5 switches on and off in play)", os::PathToUtf8(reshade),
                        own ? "the port's own build, no user interface" : "not the port's own build: Reinstall DLSS 5 puts it back");
                return true;
            }
            LogWarn("neural rendering: ReShade ({}) could not be loaded (error {}); DLSS 5 stays off this session", os::PathToUtf8(reshade),
                    static_cast<unsigned>(GetLastError()));
        } else {
            LogWarn("neural rendering: the DLSS 5 route's ReShade (dxgi.dll) is missing next to pt.exe; DLSS 5 stays off this session");
        }
    }
    if (LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!load) LogInfo("neural rendering: the DLSS 5 route is installed but not usable this start: Windows' dxgi.dll loaded, ReShade stays out of the game");
    } else {
        LogWarn("neural rendering: Windows' dxgi.dll could not be loaded first (error {})", static_cast<unsigned>(GetLastError()));
    }
    return false;
#else
    (void)game_dir;
    (void)install_present;
    (void)load;
    return false;
#endif
}

std::string FileSha256(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    unsigned char digest[32];
    if (!integrity::Sha256(bytes.data(), bytes.size(), digest)) return {};
    return Hex(digest);
}

std::string DownloadFile(const std::string& url, const std::filesystem::path& dest, uint64_t max_bytes, const std::function<void(int)>& percent,
                         const std::function<bool()>& cancelled, std::string* error) {
    return Download(url, dest, max_bytes, percent, cancelled, error);
}

std::filesystem::path ToolPath(const std::filesystem::path& work_dir) {
    return work_dir / ("autopilot-" + std::string(kToolVersion)) / std::string(kToolExe);
}

#ifdef _WIN32
bool PrepareTool(const std::filesystem::path& work_dir, const std::function<void(int)>& percent, const std::function<bool()>& cancelled,
                 std::string* failure_key, std::string* detail) {
    auto fail = [&](const char* key, const std::string& why) {
        if (failure_key) *failure_key = key;
        if (detail) *detail = why;
        LogWarn("dlss5 unlock: {}", why);
        return false;
    };
    const std::filesystem::path tool = ToolPath(work_dir);
    std::error_code ec;
    std::filesystem::create_directories(tool.parent_path(), ec);
    if (FileSha256(tool) == kToolExeSha256) {
        LogInfo("dlss5 unlock: {} is already here and matches its SHA-256", kToolExe);
        return true;
    }
    ToolSource source;
    std::string exe_sha = std::string(kToolExeSha256);
    std::filesystem::path zip;
    {
        const std::string pinned_url = EnvOr("PT_NR_TOOL_URL", kToolUrl);
        zip = work_dir / std::string(std::string_view(pinned_url).substr(pinned_url.rfind('/') + 1));
        std::string pinned_fail;
        uint64_t zip_size = 0;
        std::string zip_sha = FileSha256Impl(zip, &zip_size);
        if (zip_sha != kToolZipSha256 || zip_size != kToolZipSize) {
            std::string err;
            LogInfo("dlss5 unlock: fetching the pinned {} {} from {}", kToolName, kToolVersion, pinned_url);
            zip_sha = Download(pinned_url, zip, kToolZipSize + 1024 * 1024, percent, cancelled, &err);
            zip_size = 0;
            if (!zip_sha.empty()) zip_sha = FileSha256Impl(zip, &zip_size);
            if (zip_sha != kToolZipSha256 || zip_size != kToolZipSize) {
                pinned_fail = err.empty() ? std::format("the pinned asset's SHA-256 {} ({} bytes) is not {} ({} bytes)", zip_sha.empty() ? "unreadable" : zip_sha,
                                                        zip_size, kToolZipSha256, kToolZipSize)
                                          : std::format("the pinned asset could not be fetched: {}", err);
            }
        }
        if (pinned_fail.empty()) {
            source = {ToolSource::Which::Pinned, "v" + std::string(kToolVersion), zip_sha, std::format("pinned {} {}", kToolName, kToolVersion)};
        } else {
            LogWarn("dlss5 unlock: {}; trying the latest release", pinned_fail);
            const std::string api = EnvOr("PT_NR_TOOL_API", kToolLatestApi);
            const auto meta = HttpGetText(api, true);
            metadata_json::Value release;
            if (!meta || !metadata_json::Reader(*meta).Parse(release) || !release.object) {
                return fail("pc_note_nr_unlock_download", std::format("neither the pinned asset nor the latest release could be read ({}; the release metadata at {} failed)",
                                                                      pinned_fail, api));
            }
            const std::string tag = MemberText(release, "tag_name");
            const std::string zip_url = AssetUrl(release, "-win64.zip");
            const std::string sums_url = AssetUrl(release, "SHA256SUMS.txt");
            if (zip_url.empty() || sums_url.empty()) {
                return fail("pc_note_nr_unlock_hash", std::format("the latest release {} has no win64 zip and SHA256SUMS.txt to verify against (pinned: {})", tag, pinned_fail));
            }
            const auto sums = HttpGetText(sums_url, false);
            if (!sums) {
                return fail("pc_note_nr_unlock_hash", std::format("the latest release {}'s SHA256SUMS.txt could not be read ({})", tag, sums_url));
            }
            const std::string want_zip = ShaFromSums(*sums, std::string_view(zip_url).substr(zip_url.rfind('/') + 1));
            const std::string want_exe = ShaFromSums(*sums, kToolSumsExePath);
            if (want_zip.size() != 64 || want_exe.size() != 64) {
                return fail("pc_note_nr_unlock_hash", std::format("the latest release {}'s SHA256SUMS.txt does not list the zip and {} (pinned: {})", tag, kToolSumsExePath, pinned_fail));
            }
            zip = work_dir / std::string(std::string_view(zip_url).substr(zip_url.rfind('/') + 1));
            std::string err;
            LogInfo("dlss5 unlock: fetching the latest {} {} from {}", kToolName, tag, zip_url);
            const std::string got = Download(zip_url, zip, 64ull * 1024 * 1024, percent, cancelled, &err);
            if (got != want_zip) {
                return fail("pc_note_nr_unlock_hash", std::format("the latest release {}'s zip SHA-256 {} did not match its SHA256SUMS.txt {} ({})", tag,
                                                                  got.empty() ? "unreadable" : got, want_zip, err));
            }
            source = {ToolSource::Which::Latest, tag, got, std::format("latest {} {} (pinned {} was gone or changed)", kToolName, tag, kToolVersion)};
            exe_sha = want_exe;
        }
    }
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    const std::filesystem::path tar = std::filesystem::path(system) / L"tar.exe";
    std::filesystem::remove(tool, ec);
    std::atomic<bool> never{false};
    const os::ProcessResult extracted =
        os::RunProcess(tar, {"-xf", os::PathToUtf8(zip), "-C", os::PathToUtf8(tool.parent_path()), std::string(kToolExe)}, tool.parent_path(),
                       work_dir / "tar.log", never, std::chrono::minutes(2));
    const std::string tool_sha = FileSha256(tool);
    if (!extracted.started || extracted.exit_code != 0 || tool_sha != exe_sha) {
        std::filesystem::remove(tool, ec);
        return fail(tool_sha.empty() || tool_sha == exe_sha ? "pc_note_nr_unlock_failed" : "pc_note_nr_unlock_hash",
                    std::format("{} could not be taken out of the {} zip (tar started {}, exit {}, SHA-256 {} wanted {})", kToolExe, source.note,
                                extracted.started, extracted.exit_code, tool_sha.empty() ? "none" : tool_sha, exe_sha));
    }
    {
        std::ofstream out(work_dir / "tool-source.txt", std::ios::binary | std::ios::trunc);
        out << (source.which == ToolSource::Which::Pinned ? "pinned" : "latest") << "\n" << source.tag << "\n" << source.zip_sha << "\n" << tool_sha << "\n";
    }
    LogInfo("dlss5 unlock: {} ready from the {} ({}), SHA-256 {}", kToolExe, source.note, source.tag, tool_sha);
    return true;
}
#else
bool PrepareTool(const std::filesystem::path&, const std::function<void(int)>&, const std::function<bool()>&, std::string* failure_key, std::string*) {
    if (failure_key) *failure_key = "pc_note_nr_unlock_failed";
    return false;
}
#endif

bool Job::Start(Kind kind, const std::filesystem::path& work_dir) {
#ifdef _WIN32
    if (state_->busy.exchange(true)) return false;
    {
        std::lock_guard lock(state_->mutex);
        const uint32_t serial = state_->status.serial;
        state_->status = Status{};
        state_->status.kind = kind;
        state_->status.phase = Phase::Downloading;
        state_->status.serial = serial;
    }
    std::thread([state = state_, kind, work_dir] {
        os::LowerCurrentThreadPriority();
        auto finish = [&](Phase phase, std::string failure, std::string detail) {
            std::lock_guard lock(state->mutex);
            state->status.phase = phase;
            state->status.percent = phase == Phase::Ready ? 100 : state->status.percent;
            state->status.failure = std::move(failure);
            state->status.detail = std::move(detail);
            ++state->status.serial;
            state->busy = false;
        };
        auto fail = [&](const char* key, const std::string& detail) {
            LogWarn("dlss5 unlock: {}", detail);
            finish(Phase::Failed, key, detail);
        };
        auto percent = [&](int p) {
            std::lock_guard lock(state->mutex);
            state->status.percent = std::clamp(p, 0, 100);
        };
        std::string failure_key, detail;
        auto cancelled = [&] { return state->cancel.load(); };
        const auto on_percent = [&](int p) { percent(p); };
        if (PrepareTool(work_dir, on_percent, cancelled, &failure_key, &detail)) {
            finish(Phase::Ready, {}, {});
        } else {
            fail(failure_key.empty() ? "pc_note_nr_unlock_failed" : failure_key.c_str(), detail);
        }
    }).detach();
    return true;
#else
    (void)kind;
    (void)work_dir;
    return false;
#endif
}

Job::Status Job::Get() const {
    std::lock_guard lock(state_->mutex);
    return state_->status;
}

bool Job::Busy() const { return state_->busy.load(); }

std::vector<std::string> TopLevelNames(const std::filesystem::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = os::PathToUtf8(it->path().filename());
        if (it->is_directory(ec)) name += "/";
        names.push_back(std::move(name));
    }
    std::sort(names.begin(), names.end());
    return names;
}

#ifdef _WIN32
namespace {
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);
std::atomic<GetAsyncKeyStateFn> g_real_get_async_key_state{nullptr};
std::atomic<bool> g_nr_hook_ready{false};
constexpr int kPressReads = 3;
std::atomic<int> g_nr_press_reads{0};
std::atomic<uint32_t> g_nr_presses_delivered{0};
std::atomic<uint32_t> g_nr_presses_started{0};
std::atomic<int> g_nr_up_reads{0};

SHORT WINAPI NrGetAsyncKeyState(int vk) {
    if (vk == kNrToggleVk) {
        int left = g_nr_press_reads.load();
        while (left > 0 && !g_nr_press_reads.compare_exchange_weak(left, left - 1)) {
        }
        if (left > 0) {
            if (left == 1) g_nr_presses_delivered.fetch_add(1);
            return static_cast<SHORT>(left == kPressReads ? 0x8001 : 0x8000);
        }
        g_nr_up_reads.fetch_add(1);
        return 0;
    }
    if (vk == kNrScreenshotVk) return 0;
    const GetAsyncKeyStateFn real = g_real_get_async_key_state.load();
    return real ? real(vk) : 0;
}

void** ImportSlot(HMODULE module, const char* dll, const char* function) {
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0 || !desc->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), function) == 0) return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}
}

bool InstallNrToggleHook() {
    if (g_nr_hook_ready.load()) return true;
    HMODULE addon = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"renodx-dlss5.addon64", &addon) || !addon) return false;
    void** slot = ImportSlot(addon, "USER32.dll", "GetAsyncKeyState");
    if (!slot) {
        static bool said = false;
        if (!said) {
            LogWarn("neural rendering: the route's add-on has no supported named USER32 GetAsyncKeyState import; DLSS 5 on and off apply at the next start");
            wchar_t name[MAX_PATH * 2]{};
            const DWORD length = GetModuleFileNameW(addon, name, MAX_PATH * 2);
            if (length > 0 && length < MAX_PATH * 2) {
                const std::filesystem::path path(name);
                std::string sha;
                try {
                    sha = FileSha256(path);
                } catch (const std::exception&) {
                }
                LogWarn("neural rendering: loaded add-on {}, sha256 {} (the installer's version may be empty for a local build)",
                        os::PathToUtf8(path), sha.empty() ? "unavailable" : sha);
            }
        }
        said = true;
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    g_real_get_async_key_state.store(reinterpret_cast<GetAsyncKeyStateFn>(*slot));
    *slot = reinterpret_cast<void*>(&NrGetAsyncKeyState);
    VirtualProtect(slot, sizeof(void*), old, &old);
    g_nr_hook_ready.store(true);
    LogInfo("neural rendering: DLSS 5 switches on and off in play (the add-on's toggle key, served to the add-on alone)");
    return true;
}

bool NrToggleHookReady() { return g_nr_hook_ready.load(); }

bool PressNrToggle() {
    if (!g_nr_hook_ready.load() || g_nr_up_reads.load() < 2) return false;
    int idle = 0;
    if (!g_nr_press_reads.compare_exchange_strong(idle, kPressReads)) return false;
    g_nr_up_reads.store(0);
    g_nr_presses_started.fetch_add(1);
    return true;
}

uint32_t NrTogglesStarted() { return g_nr_presses_started.load(); }
uint32_t NrTogglesDelivered() { return g_nr_presses_delivered.load(); }
#else
bool InstallNrToggleHook() { return false; }
bool NrToggleHookReady() { return false; }
bool PressNrToggle() { return false; }
uint32_t NrTogglesStarted() { return 0; }
uint32_t NrTogglesDelivered() { return 0; }
#endif

int RetireRouteFiles(const std::filesystem::path& game_dir, const std::filesystem::path& archive_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path route = game_dir / "pt-dlss5-route.json", autopilot = game_dir / kManifest;
    if (!fs::is_regular_file(route, ec) && !fs::is_regular_file(autopilot, ec)) return 0;
    std::vector<std::pair<std::string, std::string>> recorded;
    std::string own_reshade;
    json::Value root;
    if (json::Reader(ReadText(route, 1 << 20)).Parse(root) && root.object) {
        if (const auto o = root.members.find("pt_reshade_sha256"); o != root.members.end()) own_reshade = o->second.text;
        if (const auto f = root.members.find("files"); f != root.members.end())
            for (const auto& e : f->second.items) {
                const auto n = e.members.find("name"), s = e.members.find("sha256");
                if (n != e.members.end() && !n->second.text.empty()) recorded.emplace_back(n->second.text, s != e.members.end() ? s->second.text : "");
            }
    }
    if (recorded.empty())
        if (const std::optional<Install> install = ReadInstall(game_dir))
            for (const std::string& f : install->files) recorded.emplace_back(f, "");
    int moved = 0;
    std::vector<std::string> kept;
    auto move = [&](const std::string& name) {
        const fs::path from = game_dir / os::PathFromUtf8(name), to = archive_dir / os::PathFromUtf8(name);
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        if (ec) {
            LogWarn("neural rendering: retired route file {} could not be moved: {}", name, ec.message());
            kept.push_back(name);
            return;
        }
        LogInfo("neural rendering: retired route file {} moved to {}", name, os::PathToUtf8(to));
        ++moved;
    };
    for (const auto& [name, sha] : recorded) {
        const fs::path rel = os::PathFromUtf8(name).lexically_normal();
        if (rel.empty() || rel.is_absolute() || rel.has_root_name() || *rel.begin() == "..") continue;
        const fs::path path = game_dir / rel;
        if (!fs::is_regular_file(path, ec)) continue;
        const bool is_dxgi = AsciiLowerCopy(name) == AsciiLowerCopy(std::string(kOwnReShadeFile));
        const std::string now = FileSha256(path);
        const bool ours = !sha.empty() ? now == sha
                        : is_dxgi      ? (!own_reshade.empty() && now == own_reshade) || IsOwnReShade(game_dir)
                                       : AsciiLowerCopy(name) == "renodx-dlss5.addon64";
        if (ours) move(name);
        else {
            LogWarn("neural rendering: retired route file {} stays (changed since the route installed it, sha256 {})", name, now.empty() ? "unreadable" : now);
            kept.push_back(name);
        }
    }
    if (const fs::path dxgi = game_dir / kOwnReShadeFile; fs::is_regular_file(dxgi, ec)) {
        const std::string now = FileSha256(dxgi);
        if ((!own_reshade.empty() && now == own_reshade) || IsOwnReShade(game_dir)) move(std::string(kOwnReShadeFile));
    }
    for (const fs::path& m : {route, autopilot})
        if (fs::is_regular_file(m, ec)) move(os::PathToUtf8(m.filename()));
    LogInfo("neural rendering: retired DLSS 5 route cleaned up: {} files moved to {}, {} kept", moved, os::PathToUtf8(archive_dir), kept.size());
    return moved;
}

}
