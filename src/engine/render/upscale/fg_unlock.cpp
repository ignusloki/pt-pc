#include "engine/render/upscale/fg_unlock.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <thread>
#include <ctime>

#include "engine/core/ascii.h"
#include "engine/core/log.h"
#include "engine/platform/http.h"
#include "engine/platform/json_value.h"
#include "engine/platform/os.h"
#include "engine/platform/self_integrity.h"
#include "engine/platform/update_check.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace pt::fg_unlock {

namespace {

std::string Hex(const unsigned char* digest) {
    static const char* hex = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    return out;
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

std::string ReadText(const std::filesystem::path& path, size_t max_bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text.size() > max_bytes ? text.substr(text.size() - max_bytes) : text;
}

std::string EnvOr(const char* name, std::string_view fallback) {
    const std::string value = os::GetEnv(name);
    return value.empty() ? std::string(fallback) : value;
}

std::string MemberText(const metadata_json::Value& v, const char* name) {
    const auto it = v.members.find(name);
    return it != v.members.end() && !it->second.object && it->second.items.empty() ? it->second.text : std::string();
}

std::filesystem::path FileUrlPath(const std::string& url) {
    return url.compare(0, 5, "file:") == 0 ? os::PathFromUtf8(url.substr(5)) : std::filesystem::path();
}

std::optional<std::string> HttpGetText(const std::string& url, bool metadata) {
    if (const std::filesystem::path f = FileUrlPath(url); !f.empty()) {
        return std::filesystem::exists(f) ? std::optional<std::string>(ReadText(f, 256 * 1024)) : std::nullopt;
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

std::pair<std::string, std::string> AssetUrlDigest(const metadata_json::Value& release, std::string_view suffix) {
    const auto it = release.members.find("assets");
    if (it == release.members.end()) return {};
    for (const metadata_json::Value& a : it->second.items) {
        const std::string low = AsciiLowerCopy(MemberText(a, "name"));
        if (low.size() >= suffix.size() && low.compare(low.size() - suffix.size(), suffix.size(), AsciiLowerCopy(std::string(suffix))) == 0) {
            std::string digest = MemberText(a, "digest");
            if (digest.compare(0, 7, "sha256:") == 0) digest = AsciiLowerCopy(digest.substr(7));
            else digest.clear();
            return {MemberText(a, "browser_download_url"), digest};
        }
    }
    return {};
}

std::string Download(const std::string& url, const std::filesystem::path& dest, uint64_t max_bytes, const std::function<void(int)>& percent,
                     const std::function<bool()>& cancelled, std::string* error) {
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
    std::filesystem::path part = dest;
    part += ".part";
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

bool HasMarker(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data.find(kProxyMarker) != std::string::npos;
}

}

std::string FileSha256(const std::filesystem::path& path) { return FileSha256Impl(path, nullptr); }

std::optional<Install> ParseInstall(std::string_view text) {
    metadata_json::Value root;
    if (!metadata_json::Reader(text).Parse(root) || !root.object) return std::nullopt;
    Install install;
    install.complete = MemberText(root, "complete") == "true";
    install.tool_tag = MemberText(root, "tool_tag");
    install.runtime = MemberText(root, "runtime");
    install.proxy = MemberText(root, "proxy");
    if (const auto f = root.members.find("files"); f != root.members.end())
        for (const metadata_json::Value& e : f->second.items) {
            if (e.is_string && !e.text.empty()) install.files.push_back(e.text);
            else if (e.object) {
                const std::string name = MemberText(e, "name");
                if (name.empty()) continue;
                install.files.push_back(name);
                if (AsciiLowerCopy(name) == AsciiLowerCopy(install.proxy)) install.proxy_sha256 = AsciiLowerCopy(MemberText(e, "sha256"));
            }
        }
    return install;
}

std::optional<Install> ReadInstall(const std::filesystem::path& game_dir) {
    const std::filesystem::path m = game_dir / std::string(kManifest);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(m, ec)) return std::nullopt;
    return ParseInstall(ReadText(m, 1 << 20));
}

bool ProxyPresent(const std::filesystem::path& game_dir) {
    std::error_code ec;
    for (const char* name : kProxyCandidates) {
        const std::filesystem::path p = game_dir / name;
        if (std::filesystem::is_regular_file(p, ec) && HasMarker(p)) return true;
    }
    return false;
}

bool Usable(const Install& install) {
    if (!install.complete || install.proxy.empty()) return false;
    const bool has_proxy = std::any_of(install.files.begin(), install.files.end(),
                                       [&](const std::string& f) { return AsciiLowerCopy(f) == AsciiLowerCopy(install.proxy); });
    const bool has_dir = std::any_of(install.files.begin(), install.files.end(),
                                     [](const std::string& f) { return AsciiLowerCopy(f).rfind(std::string(kDir) + "/", 0) == 0 || AsciiLowerCopy(f).rfind(std::string(kDir) + "\\", 0) == 0; });
    return has_proxy && has_dir;
}

bool RecordedProxyPresent(const std::filesystem::path& game_dir) {
    const std::optional<Install> install = ReadInstall(game_dir);
    if (!install || !Usable(*install)) return false;
    std::error_code ec;
    const std::filesystem::path proxy = game_dir / os::PathFromUtf8(install->proxy);
    return std::filesystem::is_regular_file(proxy, ec) && HasMarker(proxy);
}

namespace {

bool ModuleLoadedFrom(const std::filesystem::path& file) {
#ifdef _WIN32
    const HMODULE module = GetModuleHandleW(file.filename().c_str());
    if (!module) return false;
    std::wstring path(32768, L'\0');
    const DWORD n = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (n == 0 || n >= path.size()) return false;
    path.resize(n);
    std::error_code ec;
    return std::filesystem::equivalent(std::filesystem::path(path), file, ec);
#else
    (void)file;
    return false;
#endif
}

std::string NowStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    return stamp;
}

bool MoveAside(const std::filesystem::path& file, const std::filesystem::path& archive_dir, std::string& where) {
    std::error_code ec;
    std::filesystem::create_directories(archive_dir, ec);
    std::filesystem::path to = archive_dir / file.filename();
    for (int i = 1; std::filesystem::exists(to, ec) && i < 100; ++i) to = archive_dir / (os::PathToUtf8(file.filename()) + "." + std::to_string(i));
    std::filesystem::rename(file, to, ec);
    if (ec) {
        std::filesystem::path beside = file;
        beside += ".pt-retired";
        ec.clear();
        std::filesystem::remove(beside, ec);
        ec.clear();
        std::filesystem::rename(file, beside, ec);
        if (ec) {
            where = ec.message();
            return false;
        }
        to = beside;
    }
    where = os::PathToUtf8(to);
    return true;
}

}

std::vector<ProxyFile> UnrecordedProxies(const std::filesystem::path& game_dir) {
    std::vector<ProxyFile> out;
    const std::optional<Install> install = ReadInstall(game_dir);
    std::error_code ec;
    for (const char* name : kProxyCandidates) {
        const std::string lower = AsciiLowerCopy(std::string(name));
        if (install && AsciiLowerCopy(install->proxy) == lower) continue;
        if (install && std::any_of(install->files.begin(), install->files.end(), [&](const std::string& f) { return AsciiLowerCopy(f) == lower; })) continue;
        const std::filesystem::path p = game_dir / name;
        if (!std::filesystem::is_regular_file(p, ec)) continue;
        ProxyFile f;
        f.name = name;
        f.sha256 = FileSha256Impl(p, &f.size);
        f.marker = HasMarker(p);
        f.loaded = ModuleLoadedFrom(p);
        out.push_back(std::move(f));
    }
    return out;
}

std::string DescribeUnrecorded(const std::vector<ProxyFile>& files) {
    std::string out;
    for (const ProxyFile& f : files) {
        if (!out.empty()) out += "; ";
        out += std::format("unrecorded proxy {}: {} ({} bytes, sha256 {}, {}), no FG unlock record", f.loaded ? "loaded" : "present", f.name, f.size,
                           f.sha256.empty() ? "unreadable" : f.sha256,
                           f.sha256 == kPinnedProxySha256 ? "the dlssg_for_sm86 proxy 1.0.6 and 1.0.7 shipped"
                           : f.marker                     ? "a dlssg_for_sm86 proxy"
                                                          : "unknown proxy");
    }
    return out;
}

std::string RetireShippedProxy(const std::filesystem::path& game_dir, const std::filesystem::path& archive_dir) {
    std::error_code ec;
    if (std::filesystem::exists(game_dir / std::string(kManifest), ec)) return {};
    const std::filesystem::path p = game_dir / "version.dll";
    uint64_t size = 0;
    if (!std::filesystem::is_regular_file(p, ec) || FileSha256Impl(p, &size) != kPinnedProxySha256) return {};
    std::string where;
    if (!MoveAside(p, archive_dir / NowStamp(), where))
        return std::format("the version.dll 1.0.6 and 1.0.7 shipped by mistake (no FG unlock record) could not be moved out: {}", where);
    return std::format("the version.dll 1.0.6 and 1.0.7 shipped by mistake (no FG unlock record) moved to {}; it stops loading at the next start", where);
}

ProxyRepair RepairRecordedProxy(const std::filesystem::path& game_dir, const std::filesystem::path& work_dir,
                                const std::vector<std::filesystem::path>& sources, const std::filesystem::path& archive_dir) {
    ProxyRepair r;
    const std::optional<Install> install = ReadInstall(game_dir);
    if (!install) return r;
    r.result = ProxyRepair::Result::Fine;
    const std::filesystem::path proxy_name = os::PathFromUtf8(install->proxy);
    if (install->proxy.empty() || proxy_name.has_parent_path() || install->proxy.find("..") != std::string::npos ||
        std::none_of(std::begin(kProxyCandidates), std::end(kProxyCandidates), [&](const char* c) { return AsciiLowerCopy(std::string(c)) == AsciiLowerCopy(install->proxy); })) {
        r.detail = "the record names no proxy";
        return r;
    }
    const std::string want = install->proxy_sha256.size() == 64 ? install->proxy_sha256 : std::string(kPinnedProxySha256);
    const std::filesystem::path live = game_dir / proxy_name;
    std::error_code ec;
    const bool present = std::filesystem::is_regular_file(live, ec);
    const std::string now = present ? FileSha256(live) : std::string();
    if (now == want) {
        r.detail = std::format("{} matches the record", install->proxy);
        return r;
    }
    const std::string problem = present ? std::format("{} has sha256 {}, not the record's {}", install->proxy, now, want)
                                        : std::format("{} is missing", install->proxy);
    std::filesystem::path from;
    bool move = false;
    for (const auto& s : sources)
        if (std::filesystem::is_regular_file(s, ec) && FileSha256(s) == want) {
            from = s;
            move = true;
            break;
        }
#ifdef _WIN32
    if (from.empty()) {
        std::ifstream ts(work_dir / "tool-source.txt");
        std::string kind, tag, zip_sha, filename;
        std::getline(ts, kind);
        std::getline(ts, tag);
        std::getline(ts, zip_sha);
        std::getline(ts, filename);
        std::filesystem::path zip = ZipPath(work_dir);
        if (!filename.empty() && !os::PathFromUtf8(filename).has_parent_path()) zip = work_dir / os::PathFromUtf8(filename);
        if (zip_sha.size() != 64) zip_sha = std::string(kToolZipSha256);
        if (std::filesystem::is_regular_file(zip, ec) && FileSha256(zip) == zip_sha) {
            const std::filesystem::path stage = work_dir / "proxy-repair";
            std::filesystem::remove_all(stage, ec);
            std::filesystem::create_directories(stage, ec);
            const std::string member = AsciiLowerCopy(install->proxy) == "version.dll" ? install->proxy : "alternatives/" + install->proxy;
            wchar_t system[MAX_PATH]{};
            GetSystemDirectoryW(system, MAX_PATH);
            std::atomic<bool> never{false};
            const auto tar = os::RunProcess(std::filesystem::path(system) / L"tar.exe", {"-xf", os::PathToUtf8(zip), "-C", os::PathToUtf8(stage), member},
                                            stage, work_dir / "proxy-repair.log", never, std::chrono::minutes(2));
            const std::filesystem::path got = stage / os::PathFromUtf8(member);
            if (tar.exit_code == 0 && FileSha256(got) == want) from = got;
        }
    }
#else
    (void)work_dir;
#endif
    if (from.empty()) {
        r.result = ProxyRepair::Result::NeedsReinstall;
        r.detail = problem + "; neither the update's backup nor a verified package has the right file";
        return r;
    }
    std::string where;
    if (present && !MoveAside(live, archive_dir / NowStamp(), where)) {
        r.result = ProxyRepair::Result::NeedsReinstall;
        r.detail = problem + "; the wrong file could not be moved out: " + where;
        return r;
    }
    if (move) std::filesystem::rename(from, live, ec);
    else std::filesystem::copy_file(from, live, std::filesystem::copy_options::none, ec);
    if (ec || FileSha256(live) != want) {
        r.result = ProxyRepair::Result::NeedsReinstall;
        r.detail = problem + "; putting the right file back failed: " + (ec ? ec.message() : std::string("its SHA-256 differs"));
        return r;
    }
    r.result = ProxyRepair::Result::Restored;
    r.detail = std::format("{}; put back from {} {}{}; it loads at the next start", problem, move ? "the update's backup" : "the verified package",
                           os::PathToUtf8(move ? from : work_dir), present ? ", the other file moved to " + where : std::string());
    return r;
}

std::string ChooseProxy(const std::filesystem::path& game_dir, std::string_view keep) {
    std::error_code ec;
    if (!keep.empty()) {
        const std::filesystem::path p = game_dir / std::string(keep);
        if (!std::filesystem::exists(p, ec) || HasMarker(p)) return std::string(keep);
    }
    for (const char* name : kProxyCandidates) {
        if (AsciiLowerCopy(name) == AsciiLowerCopy(std::string(kDlss5Proxy))) continue;
        const std::filesystem::path p = game_dir / name;
        if (!std::filesystem::exists(p, ec) || HasMarker(p)) return name;
    }
    return {};
}

RouteReport ReadRouteReport(const std::filesystem::path& game_dir) {
    RouteReport report;
    const std::filesystem::path logs = game_dir / std::string(kDir) / "logs";
    std::error_code ec;
    if (!std::filesystem::is_directory(logs, ec)) return report;
    std::filesystem::path newest;
    std::filesystem::file_time_type best{};
    for (std::filesystem::directory_iterator it(logs, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = os::PathToUtf8(it->path().filename());
        if (name.rfind("backend_", 0) == 0 && name.size() > 5 && name.substr(name.size() - 6) == ".jsonl") {
            const auto t = it->last_write_time(ec);
            if (newest.empty() || t > best) {
                best = t;
                newest = it->path();
            }
        }
    }
    if (newest.empty()) return report;
    const std::string text = ReadText(newest, 512 * 1024);
    std::string_view rest = text;
    while (!rest.empty()) {
        const size_t nl = rest.find('\n');
        std::string_view line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
        if (line.find("\"active\":true") != std::string_view::npos || line.find("\"active\": true") != std::string_view::npos ||
            line.find("route active=true") != std::string_view::npos) {
            report.state = RouteReport::State::Active;
            report.line = std::string(line);
        } else if (report.state != RouteReport::State::Active &&
                   (line.find("fallback") != std::string_view::npos || line.find("\"install\":false") != std::string_view::npos)) {
            report.state = RouteReport::State::Fallback;
            report.line = std::string(line);
        }
    }
    return report;
}

std::filesystem::path ZipPath(const std::filesystem::path& work_dir) {
    const std::string pinned_url = EnvOr("PT_FG_TOOL_URL", kToolUrl);
    return work_dir / std::string(std::string_view(pinned_url).substr(pinned_url.rfind('/') + 1));
}

bool PrepareTool(const std::filesystem::path& work_dir, const std::function<void(int)>& percent, const std::function<bool()>& cancelled,
                 std::string* failure_key, std::string* detail) {
    auto fail = [&](const char* key, const std::string& why) {
        if (failure_key) *failure_key = key;
        if (detail) *detail = why;
        LogWarn("fg unlock: {}", why);
        return false;
    };
    std::error_code ec;
    std::filesystem::create_directories(work_dir, ec);
    const std::string pinned_url = EnvOr("PT_FG_TOOL_URL", kToolUrl);
    std::filesystem::path zip = work_dir / std::string(std::string_view(pinned_url).substr(pinned_url.rfind('/') + 1));
    ToolSource source;
    std::string pinned_fail;
    uint64_t size = 0;
    std::string sha = FileSha256Impl(zip, &size);
    if (sha != kToolZipSha256 || size != kToolZipSize) {
        std::string err;
        LogInfo("fg unlock: fetching the pinned {} {} from {}", kToolName, kToolVersion, pinned_url);
        sha = Download(pinned_url, zip, kToolZipSize + 1024 * 1024, percent, cancelled, &err);
        size = 0;
        if (!sha.empty()) sha = FileSha256Impl(zip, &size);
        if (sha != kToolZipSha256 || size != kToolZipSize) {
            pinned_fail = err.empty() ? std::format("the pinned asset's SHA-256 {} ({} bytes) is not {} ({} bytes)", sha.empty() ? "unreadable" : sha,
                                                    size, kToolZipSha256, kToolZipSize)
                                      : std::format("the pinned asset could not be fetched: {}", err);
        }
    }
    if (pinned_fail.empty()) {
        source = {ToolSource::Which::Pinned, "v" + std::string(kToolVersion), sha, std::format("pinned {} {}", kToolName, kToolVersion)};
    } else {
        LogWarn("fg unlock: {}; trying the latest release", pinned_fail);
        const std::string api = EnvOr("PT_FG_TOOL_API", kToolLatestApi);
        const auto meta = HttpGetText(api, true);
        metadata_json::Value release;
        if (!meta || !metadata_json::Reader(*meta).Parse(release) || !release.object) {
            return fail("pc_note_fg_unlock_download", std::format("neither the pinned asset nor the latest release could be read ({}; the release metadata at {} failed)",
                                                                  pinned_fail, api));
        }
        const std::string tag = MemberText(release, "tag_name");
        const auto [zip_url, digest] = AssetUrlDigest(release, "-x64.zip");
        if (zip_url.empty() || digest.size() != 64) {
            return fail("pc_note_fg_unlock_hash", std::format("the latest release {} has no x64 zip with a SHA-256 digest to verify against (pinned: {})", tag, pinned_fail));
        }
        zip = work_dir / std::string(std::string_view(zip_url).substr(zip_url.rfind('/') + 1));
        std::string err;
        LogInfo("fg unlock: fetching the latest {} {} from {}", kToolName, tag, zip_url);
        const std::string got = Download(zip_url, zip, 256ull * 1024 * 1024, percent, cancelled, &err);
        if (got != digest) {
            return fail("pc_note_fg_unlock_hash", std::format("the latest release {}'s zip SHA-256 {} did not match GitHub's asset digest {} ({})", tag,
                                                              got.empty() ? "unreadable" : got, digest, err));
        }
        source = {ToolSource::Which::Latest, tag, got, std::format("latest {} {} (pinned {} was gone or changed)", kToolName, tag, kToolVersion)};
    }
    {
        std::ofstream out(work_dir / "tool-source.txt", std::ios::binary | std::ios::trunc);
        out << (source.which == ToolSource::Which::Pinned ? "pinned" : "latest") << "\n" << source.tag << "\n" << source.zip_sha << "\n"
            << os::PathToUtf8(zip.filename()) << "\n";
    }
    LogInfo("fg unlock: {} ready from the {} ({})", os::PathToUtf8(zip.filename()), source.note, source.tag);
    return true;
}

bool InstallLive(const std::filesystem::path& work_dir, const std::filesystem::path& game_dir, std::string& detail,
                 const std::atomic<bool>* cancelled) {
#ifdef _WIN32
    std::atomic<bool> never_cancelled{false};
    const auto& cancel = cancelled ? *cancelled : never_cancelled;
    if (cancel.load()) { detail = "installation cancelled"; return false; }
    auto zip = ZipPath(work_dir);
    std::ifstream source(work_dir / "tool-source.txt");
    std::string kind, tag, expected;
    std::getline(source, kind); std::getline(source, tag); std::getline(source, expected);
    std::string filename;
    if (std::getline(source, filename) && !filename.empty()) {
        const auto name = os::PathFromUtf8(filename);
        if (name.has_parent_path() || name.is_absolute()) { detail = "invalid package filename"; return false; }
        zip = work_dir / name;
    }
    if (expected.size() != 64 || FileSha256(zip) != expected) {
        detail = "the verified package changed before installation";
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(game_dir / kManifest, ec) || ProxyPresent(game_dir) || std::filesystem::exists(game_dir / kDir, ec)) {
        detail = "an existing FG package must be removed before a first live install";
        return false;
    }
    const auto proxy = ChooseProxy(game_dir, {});
    if (proxy.empty()) { detail = "all FG proxy slots are occupied"; return false; }
    const auto stage = work_dir / "live-stage";
    std::filesystem::create_directories(stage, ec);
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    const auto tar = os::RunProcess(std::filesystem::path(system) / L"tar.exe",
        {"-xf", os::PathToUtf8(zip), "-C", os::PathToUtf8(stage)}, stage, work_dir / "live-tar.log", cancel, std::chrono::minutes(2));
    if (tar.exit_code != 0) { detail = "the verified package could not be extracted"; return false; }
    metadata_json::Value manifest;
    if (!metadata_json::Reader(ReadText(stage / "manifest.json", 1 << 20)).Parse(manifest) || !manifest.object) {
        detail = "the package manifest is missing"; return false;
    }
    const auto files = manifest.members.find("files");
    if (files == manifest.members.end()) { detail = "the package manifest has no files"; return false; }
    struct File { std::filesystem::path source, dest; std::string name, hash; };
    std::vector<File> selected;
    const std::string proxy_source = proxy == "version.dll" ? proxy : "alternatives/" + proxy;
    for (const auto& f : files->second.items) {
        const auto name = MemberText(f, "path");
        if (name != proxy_source && name != kIni && name.rfind(std::string(kDir) + "/", 0) != 0) continue;
        const auto relative = os::PathFromUtf8(name);
        if (relative.is_absolute() || name.find("..") != std::string::npos || name.find(':') != std::string::npos) {
            detail = "unsafe package path"; return false;
        }
        const std::string dest = name == proxy_source ? proxy : name;
        const auto hash = MemberText(f, "sha256");
        if (hash.size() != 64 || FileSha256(stage / relative) != hash || std::filesystem::exists(game_dir / dest, ec)) {
            detail = "package file failed verification or destination is occupied: " + dest; return false;
        }
        selected.push_back({stage / relative, game_dir / dest, dest, hash});
    }
    if (selected.size() < 5 || std::none_of(selected.begin(), selected.end(), [&](const File& f) { return f.name == proxy; })) {
        detail = "the package is incomplete"; return false;
    }
    std::stable_sort(selected.begin(), selected.end(), [&](const File& a, const File& b) { return (a.name == proxy) < (b.name == proxy); });
    std::vector<std::filesystem::path> placed;
    auto rollback = [&] {
        for (const auto& p : placed) std::filesystem::remove(p, ec);
        std::filesystem::remove_all(game_dir / kDir, ec);
        std::filesystem::remove(game_dir / kManifest, ec);
    };
    for (const auto& f : selected) {
        if (cancel.load()) { detail = "installation cancelled"; rollback(); return false; }
        std::filesystem::create_directories(f.dest.parent_path(), ec);
        if (!std::filesystem::copy_file(f.source, f.dest, std::filesystem::copy_options::none, ec)) {
            detail = "cannot install " + f.name + ": " + ec.message(); rollback(); return false;
        }
        placed.push_back(f.dest);
    }
    if (cancel.load()) { detail = "installation cancelled"; rollback(); return false; }
    std::ofstream out(game_dir / kManifest, std::ios::binary | std::ios::trunc);
    out << "{\"complete\":true,\"tool_tag\":\"" << tag << "\",\"runtime\":\""
        << MemberText(manifest, "runtime_model") << "\",\"proxy\":\"" << proxy << "\",\"files\":[";
    for (size_t i = 0; i < selected.size(); ++i) {
        if (i) out << ',';
        out << "{\"name\":\"" << selected[i].name << "\",\"sha256\":\"" << selected[i].hash << "\"}";
    }
    out << "]}";
    out.close();
    if (!out) { detail = "cannot write the install record"; rollback(); return false; }
    detail = "installed verified package " + tag + " while the game remained running";
    LogInfo("fg unlock: {}", detail);
    return true;
#else
    detail = "live FG installation requires Windows"; return false;
#endif
}

bool LoadLiveProxy(const std::filesystem::path& game_dir, std::string& detail) {
#ifdef _WIN32
    const auto install = ReadInstall(game_dir);
    if (!install || !Usable(*install)) { detail = "the FG install is incomplete"; return false; }
    const auto path = game_dir / install->proxy;
    if (!HasMarker(path)) { detail = "the installed proxy is not the community FG package"; return false; }
    const HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) { detail = std::format("loading the FG proxy failed ({})", GetLastError()); return false; }
    if (auto version = reinterpret_cast<DWORD(WINAPI*)(LPCWSTR, LPDWORD)>(GetProcAddress(module, "GetFileVersionInfoSizeW"))) {
        DWORD unused{}; version((game_dir / L"pt.exe").c_str(), &unused);
    } else if (auto time = reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(module, "timeGetTime"))) {
        time();
    } else if (auto options = reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(module, "SymGetOptions"))) {
        options();
    }
    LogInfo("fg unlock: explicitly loaded {} into running process {}", os::PathToUtf8(path), GetCurrentProcessId());
    return true;
#else
    detail = "live FG activation requires Windows"; return false;
#endif
}

bool Job::Start(Kind kind, const std::filesystem::path& work_dir, const std::filesystem::path& live_install_dir) {
#ifdef _WIN32
    if (!live_install_dir.empty() && kind != Kind::Install) return false;
    if (state_->busy.exchange(true)) return false;
    state_->cancel = false;
    {
        std::lock_guard lock(state_->mutex);
        const uint32_t serial = state_->status.serial;
        state_->status = Status{};
        state_->status.kind = kind;
        state_->status.phase = Phase::Downloading;
        state_->status.serial = serial;
    }
    std::thread([state = state_, work_dir, live_install_dir] {
        os::LowerCurrentThreadPriority();
        auto percent = [&](int p) {
            std::lock_guard lock(state->mutex);
            state->status.percent = std::clamp(p, 0, 100);
        };
        std::string key, detail;
        bool ok = PrepareTool(work_dir, [&](int p) { percent(p); }, [&] { return state->cancel.load(); }, &key, &detail);
        const bool live = ok && !live_install_dir.empty();
        if (live) ok = InstallLive(work_dir, live_install_dir, detail, &state->cancel);
        std::lock_guard lock(state->mutex);
        state->status.installed_live = live && ok;
        state->status.phase = ok ? Phase::Ready : Phase::Failed;
        state->status.percent = ok ? 100 : state->status.percent;
        state->status.failure = ok ? std::string() : (key.empty() ? "pc_note_fg_unlock_failed" : key);
        state->status.detail = detail;
        ++state->status.serial;
        state->busy = false;
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

}
