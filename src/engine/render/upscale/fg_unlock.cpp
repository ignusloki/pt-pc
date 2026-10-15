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

constexpr uint32_t kNvidia = 0x10DE;

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

std::string MemberText(const json::Value& v, const char* name) {
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

std::pair<std::string, std::string> AssetUrlDigest(const json::Value& release, std::string_view suffix) {
    const auto it = release.members.find("assets");
    if (it == release.members.end()) return {};
    for (const json::Value& a : it->second.items) {
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

bool HasMarker(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data.find(kProxyMarker) != std::string::npos;
}

}

std::string FileSha256(const std::filesystem::path& path) { return FileSha256Impl(path, nullptr); }

bool Applicable(uint32_t vendor_id, std::string_view dlssg_note) { return vendor_id == kNvidia && dlssg_note == "pc_note_dlssg_gpu"; }

std::optional<Install> ParseInstall(std::string_view text) {
    json::Value root;
    if (!json::Reader(text).Parse(root) || !root.object) return std::nullopt;
    Install install;
    install.complete = MemberText(root, "complete") == "true";
    install.tool_tag = MemberText(root, "tool_tag");
    install.runtime = MemberText(root, "runtime");
    install.proxy = MemberText(root, "proxy");
    if (const auto f = root.members.find("files"); f != root.members.end())
        for (const json::Value& e : f->second.items) {
            if (e.is_string && !e.text.empty()) install.files.push_back(e.text);
            else if (e.object) { const std::string name = MemberText(e, "name"); if (!name.empty()) install.files.push_back(name); }
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
        json::Value release;
        if (!meta || !json::Reader(*meta).Parse(release) || !release.object) {
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
        out << (source.which == ToolSource::Which::Pinned ? "pinned" : "latest") << "\n" << source.tag << "\n" << source.zip_sha << "\n";
    }
    LogInfo("fg unlock: {} ready from the {} ({})", os::PathToUtf8(zip.filename()), source.note, source.tag);
    return true;
}

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
    std::thread([state = state_, work_dir] {
        os::LowerCurrentThreadPriority();
        auto percent = [&](int p) {
            std::lock_guard lock(state->mutex);
            state->status.percent = std::clamp(p, 0, 100);
        };
        std::string key, detail;
        const bool ok = PrepareTool(work_dir, [&](int p) { percent(p); }, [&] { return state->cancel.load(); }, &key, &detail);
        std::lock_guard lock(state->mutex);
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
