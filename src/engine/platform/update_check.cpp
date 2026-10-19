#include "engine/platform/update_check.h"

#include <cctype>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <map>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "engine/platform/release_key.h"
#include "engine/platform/signed_trailer.h"

#include "engine/platform/ed25519.h"
#include "engine/platform/http.h"
#include "engine/platform/json_value.h"

#if __has_include("pt_version.h")
#include "pt_version.h"
#endif
#ifndef PT_VERSION
#define PT_VERSION "0.0.0-dev"
#endif
#ifndef PT_UPDATE_MANIFEST_URL
#define PT_UPDATE_MANIFEST_URL ""
#endif

namespace pt::update {
namespace {
using metadata_json::Value;
using metadata_json::Reader;
using metadata_json::Text;
bool Placeholder(std::string_view url) {
    const auto scheme = url.find("://");
    const auto host_start = scheme == std::string_view::npos ? 0 : scheme + 3;
    const auto host_end = url.find_first_of("/:", host_start);
    const std::string_view host = url.substr(host_start, host_end == std::string_view::npos ? std::string_view::npos : host_end - host_start);
    return host.empty() || host.ends_with(".invalid") || host == "invalid";
}

bool VersionTag(std::string_view tag) {
    if (tag.size() > 64) return false;
    if (tag.starts_with('v') || tag.starts_with('V')) tag.remove_prefix(1);
    bool digit = false;
    for (const char c : tag) {
        if (c >= '0' && c <= '9') digit = true;
        else if (c == '.' && digit) digit = false;
        else return false;
    }
    return digit;
}

std::string_view AssetName(std::string_view platform) {
    if (platform == "windows") return "P.T.PC.Port.Setup.exe";
    if (platform == "linux") return "P.T.PC.Port.Setup-linux";
    if (platform == "macos-arm64") return "P.T.PC.Port-macOS-arm64.zip";
    if (platform == "macos-x64") return "P.T.PC.Port-macOS-x64.zip";
    return {};
}
}

std::string ManifestUrl() {
    if (const char* env = std::getenv("PT_UPDATE_MANIFEST_URL"); env && *env) return env;
    return PT_UPDATE_MANIFEST_URL;
}

std::string_view CurrentVersion() { return PT_VERSION; }

std::string_view Platform() {
#ifdef __ANDROID__
    return "android";
#endif
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
#if defined(__aarch64__)
    return "macos-arm64";
#else
    return "macos-x64";
#endif
#else
    return "linux";
#endif
}

int CompareVersions(std::string_view a, std::string_view b) {
    auto split = [](std::string_view v, std::string_view& suffix) {
        std::vector<long> parts;
        if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.remove_prefix(1);
        const auto dash = v.find('-');
        suffix = dash == std::string_view::npos ? std::string_view() : v.substr(dash + 1);
        v = v.substr(0, dash);
        while (!v.empty()) {
            const auto dot = v.find('.');
            const std::string_view part = v.substr(0, dot);
            long n = 0;
            std::from_chars(part.data(), part.data() + part.size(), n);
            parts.push_back(n);
            if (dot == std::string_view::npos) break;
            v.remove_prefix(dot + 1);
        }
        return parts;
    };
    std::string_view sa, sb;
    const auto pa = split(a, sa), pb = split(b, sb);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (sa.empty() != sb.empty()) return sa.empty() ? 1 : -1;
    return sa == sb ? 0 : (sa < sb ? -1 : 1);
}

std::optional<Release> ParseManifest(std::string_view json, std::string_view platform) {
    Value root;
    if (!Reader(json).Parse(root) || !root.object) return std::nullopt;
    if (const std::string* tag = Text(root, "tag_name")) {
        for (const char* flag : {"draft", "prerelease"}) {
            if (const auto it = root.members.find(flag); it != root.members.end() && it->second.text != "false") return std::nullopt;
        }
        const std::string* page = Text(root, "html_url");
        if (!VersionTag(*tag) || !page || !page->starts_with("https://")) return std::nullopt;
        Release release{*tag, *page, {}};
        if (const std::string* body = Text(root, "body")) release.notes = body->substr(0, std::min(body->find('\n'), size_t(200)));
        release.page = *page;
        if (const auto assets = root.members.find("assets"); assets != root.members.end()) {
            const std::string_view wanted = AssetName(platform);
            const std::string_view package = PackageAsset(platform);
            for (const Value& asset : assets->second.items) {
                const std::string* name = Text(asset, "name");
                const std::string* url = Text(asset, "browser_download_url");
                if (!name || !url || url->size() > 2048) continue;
                if (!wanted.empty() && *name == wanted && url->starts_with("https://") && release.url == *page) release.url = *url;
                if (!AllowedAssetUrl(*url)) continue;
                if (!package.empty() && *name == package) {
                    release.package_url = *url;
                    if (const auto size = asset.members.find("size"); size != asset.members.end() && !size->second.is_string)
                        release.package_size = std::strtoull(size->second.text.c_str(), nullptr, 10);
                }
            }
        }
        return release;
    }
    const std::string* version = Text(root, "version");
    if (!version) return std::nullopt;
    Release release;
    release.version = *version;
    if (const std::string* url = Text(root, "url")) release.url = *url;
    if (const std::string* notes = Text(root, "notes")) release.notes = *notes;
    if (const auto platforms = root.members.find("platforms"); platforms != root.members.end()) {
        if (const auto entry = platforms->second.members.find(std::string(platform)); entry != platforms->second.members.end()) {
            if (const std::string* url = Text(entry->second, "url")) release.url = *url;
        }
    }
    if (release.url.size() > 512) release.url.resize(512);
    if (release.notes.size() > 200) release.notes.resize(200);
    return release;
}

std::string_view PackageAsset(std::string_view platform) {
    if (platform == "windows") return "P.T.PC.Port.Setup.exe";
    if (platform == "linux") return "P.T.PC.Port.Setup-linux";
    return {};
}

bool TestOverrides() {
    const char* flag = std::getenv("PT_UPDATE_TEST");
    if (!flag || std::string_view(flag) != "1") return false;
#ifdef _WIN32
    HANDLE token = nullptr;
    bool elevated = true;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) elevated = elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return !elevated;
#else
    return geteuid() != 0;
#endif
}

namespace {
bool Loopback(std::string_view url) {
    const std::string host = http::UrlHost(url);
    return TestOverrides() && (host == "127.0.0.1" || host == "localhost");
}
}

bool AllowedAssetUrl(std::string_view url) {
    if (Loopback(url)) return true;
    if (http::UrlScheme(url) != "https") return false;
    const std::string host = http::UrlHost(url);
    return host == "github.com" || host == "objects.githubusercontent.com" || host == "release-assets.githubusercontent.com";
}

bool AllowedMetadataUrl(std::string_view url) {
    return AllowedAssetUrl(url) || (http::UrlScheme(url) == "https" && http::UrlHost(url) == "api.github.com");
}

std::string ReleasePublicKey() {
    if (TestOverrides()) {
        if (const char* key = std::getenv("PT_UPDATE_TEST_PUBLIC_KEY"); key && *key) {
            if (auto bytes = ed25519::FromHex(key, 32)) return *bytes;
        }
    }
    return ed25519::FromHex(kReleasePublicKeyHex, 32).value_or(std::string());
}

std::optional<UpdateManifest> VerifyManifest(std::string_view manifest, std::string_view signature_text, std::string_view public_key, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<UpdateManifest> {
        if (error) *error = why;
        return std::nullopt;
    };
    const auto signature = ed25519::FromHex(signature_text, 64);
    if (!signature) return fail("the signature file is not a signature");
    if (public_key.size() != 32) return fail("no release key");
    if (!ed25519::Verify(reinterpret_cast<const uint8_t*>(signature->data()), reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size(),
                         reinterpret_cast<const uint8_t*>(public_key.data())))
        return fail("the update manifest's signature is not valid");
    Value root;
    if (!Reader(manifest).Parse(root) || !root.object) return fail("the update manifest is not valid");
    auto number = [](const Value* value) -> std::optional<uint64_t> {
        if (!value || value->is_string || value->object || value->text.empty() || value->text.size() > 19) return std::nullopt;
        uint64_t n = 0;
        if (std::from_chars(value->text.data(), value->text.data() + value->text.size(), n).ec != std::errc()) return std::nullopt;
        return n;
    };
    auto member = [](const Value& object, const char* key) -> const Value* {
        const auto it = object.members.find(key);
        return it == object.members.end() ? nullptr : &it->second;
    };
    auto hex64 = [](const std::string* text) {
        return text && text->size() == 64 && std::all_of(text->begin(), text->end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    };
    if (number(member(root, "format")) != 1u) return fail("the update manifest has an unknown format");
    const std::string* version = Text(root, "version");
    if (!version || !VersionTag(*version)) return fail("the update manifest has no version");
    UpdateManifest out;
    out.version = *version;
    if (const Value* packages = member(root, "packages"); packages && packages->object) {
        for (const auto& [platform, entry] : packages->members) {
            const std::string* name = Text(entry, "name");
            const auto size = number(member(entry, "size"));
            const std::string* sha = Text(entry, "sha256");
            if (!name || !size || !hex64(sha)) return fail("the update manifest lists a package without its size or SHA-256");
            out.packages[platform] = {*name, *size, *sha};
        }
    }
    if (const Value* files = member(root, "files"); files && files->object) {
        for (const auto& [platform, list] : files->members) {
            auto& entries = out.files[platform];
            for (const Value& entry : list.items) {
                const std::string* path = Text(entry, "path");
                const auto size = number(member(entry, "size"));
                const std::string* sha = Text(entry, "sha256");
                if (!path || !size || !hex64(sha)) return fail("the update manifest lists a file without its size or SHA-256");
                FileEntry file{*path, *size, *sha};
                const Value* offset = member(entry, "offset");
                const Value* packed = member(entry, "packed");
                if (offset || packed) {
                    const auto at = number(offset), length = number(packed);
                    const auto package = out.packages.find(platform);
                    if (!at || !length || *length == 0 || (package != out.packages.end() && (*at > package->second.size || *length > package->second.size - *at)))
                        return fail("the update manifest lists a file outside its package");
                    file.offset = *at;
                    file.packed = *length;
                }
                entries.push_back(std::move(file));
            }
        }
    }
    if (out.packages.empty()) return fail("the update manifest lists no package");
    return out;
}

namespace {
std::optional<UpdateManifest> CheckEmbedded(const Release& release, const std::string& manifest, const std::string& signature, uint64_t body, std::string* error) {
    auto verified = VerifyManifest(manifest, ed25519::ToHex(reinterpret_cast<const uint8_t*>(signature.data()), signature.size()), ReleasePublicKey(), error);
    if (!verified) return std::nullopt;
    std::string tag = release.version;
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
    std::string version = verified->version;
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
    if (version != tag) {
        if (error) *error = "the update manifest is for version " + version + ", not " + tag;
        return std::nullopt;
    }
    const auto package = verified->packages.find(std::string(Platform()));
    if (package == verified->packages.end() || package->second.name != PackageAsset(Platform())) {
        if (error) *error = "the update manifest has no package for this system";
        return std::nullopt;
    }
    if (package->second.size != body) {
        if (error) *error = "the update manifest names another package than the one it is in";
        return std::nullopt;
    }
    return verified;
}
}

std::optional<UpdateManifest> FetchSignedManifest(const Release& release, std::string* error, bool* range_ignored) {
    if (range_ignored) *range_ignored = false;
    if (release.package_url.empty() || release.package_size < trailer::kFooterSize) {
        if (error) *error = "the release has no package for this system";
        return std::nullopt;
    }
    std::string url = release.package_url;
    auto get = [&](uint64_t first, uint64_t last, std::string& body) {
        http::FetchRequest request;
        request.url = url;
        request.timeout_ms = 10000;
        request.max_bytes = last - first + 1;
        request.range = "bytes=" + std::to_string(first) + "-" + std::to_string(last);
        request.allow = [](const std::string& u) { return AllowedAssetUrl(u); };
        request.write = [&](const char* data, size_t size) { body.append(data, size); return true; };
        const auto result = http::Fetch(request);
        if (result.range_ignored && range_ignored) *range_ignored = true;
        if (!result.ok && error) *error = "could not read the end of " + url.substr(url.rfind('/') + 1) + ": " + result.error;
        if (result.ok && !result.final_url.empty()) url = result.final_url;
        return result.ok && body.size() == last - first + 1;
    };
    const uint64_t size = release.package_size;
    std::string last;
    if (!get(size - trailer::kFooterSize, size - 1, last)) return std::nullopt;
    const auto footer = trailer::Parse(reinterpret_cast<const unsigned char*>(last.data()), size);
    if (!footer) {
        if (error) *error = "the package carries no signed update manifest";
        return std::nullopt;
    }
    std::string signed_part;
    if (!get(footer->manifest_offset, footer->signature_offset + footer->signature_length - 1, signed_part)) return std::nullopt;
    return CheckEmbedded(release, signed_part.substr(0, size_t(footer->manifest_length)), signed_part.substr(size_t(footer->manifest_length)),
                         footer->body(), error);
}

std::optional<UpdateManifest> ReadSignedManifest(const Release& release, const std::filesystem::path& package, std::string* error) {
    std::string manifest, signature;
    const auto footer = trailer::Read(package);
    if (!footer || !trailer::ReadSigned(package, manifest, signature)) {
        if (error) *error = "the package carries no signed update manifest";
        return std::nullopt;
    }
    return CheckEmbedded(release, manifest, signature, footer->body(), error);
}

uint64_t EstimateDownload(const UpdateManifest& manifest, std::string_view platform, const std::filesystem::path& install) {
    const auto package = manifest.packages.find(std::string(platform));
    const auto files = manifest.files.find(std::string(platform));
    if (package == manifest.packages.end()) return 0;
    if (files == manifest.files.end() || files->second.empty()) return package->second.size;
    std::map<std::string, std::string> recorded;
    std::ifstream in(install / "pt-install-manifest.txt", std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("file=") && line.size() > 5 + 65) recorded[line.substr(5 + 65)] = line.substr(5, 64);
    }
    uint64_t needed = 0;
    for (const FileEntry& file : files->second) {
        if (!file.packed) return package->second.size;
        const auto it = recorded.find(file.path);
        std::error_code ec;
        const std::filesystem::path local = install / std::filesystem::path(std::u8string(file.path.begin(), file.path.end()));
        const auto size = std::filesystem::file_size(local, ec);
        if (it == recorded.end() || it->second != file.sha256 || ec || size != file.size) needed += file.packed;
    }
    return double(needed) > kRangeShare * double(package->second.size) ? package->second.size : needed;
}

void Checker::Start(const std::filesystem::path& install, Fetch fetch) {
    if (state_) return;
    state_ = std::make_shared<State>();
    const std::string url = ManifestUrl();
    if (url.empty() || Placeholder(url) || !url.starts_with("https://")) {
        state_->done = true;
        return;
    }
    try {
        std::thread([state = state_, url, install, fetch] {
            try {
                http::Request request;
                request.url = url;
                request.timeout_ms = 5000;
                request.follow_redirects = true;
                request.max_body = 512 * 1024;
                if (const auto response = (fetch ? fetch : http::Get)(request); response && response->status == 200) {
                    if (auto release = ParseManifest(response->body, Platform()); release && CompareVersions(release->version, CurrentVersion()) > 0) {
                        if (!release->package_url.empty() && release->package_size) {
                            std::string error;
                            bool range_ignored = false;
                            const auto manifest = FetchSignedManifest(*release, &error, &range_ignored);
                            release->signed_update = manifest.has_value() || range_ignored;
                            if (manifest && !install.empty()) release->download_bytes = EstimateDownload(*manifest, Platform(), install);
                            else if (range_ignored) release->download_bytes = release->package_size;
                        }
                        std::lock_guard lock(state->mutex);
                        state->newer = std::move(release);
                    }
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "update: check failed: %s\n", e.what());
            } catch (...) {
                std::fputs("update: check failed with an unknown exception\n", stderr);
            }
            state->done = true;
        }).detach();
    } catch (const std::exception& e) {
        state_->done = true;
        std::fprintf(stderr, "update: could not start check: %s\n", e.what());
    } catch (...) {
        state_->done = true;
        std::fputs("update: could not start check\n", stderr);
    }
}

void Checker::Fake(std::string_view version) {
    if (state_) return;
    state_ = std::make_shared<State>();
    if (CompareVersions(version, CurrentVersion()) > 0) {
        Release release;
        release.version = std::string(version);
        release.url = "(fake update, --fake-update)";
        release.signed_update = !PackageAsset(Platform()).empty();
        if (const char* bytes = std::getenv("PT_UPDATE_TEST_FAKE_BYTES"); bytes && TestOverrides()) release.download_bytes = std::strtoull(bytes, nullptr, 10);
        std::lock_guard lock(state_->mutex);
        state_->newer = std::move(release);
    }
    state_->done = true;
}

std::optional<Release> Checker::Newer() const {
    if (!state_) return std::nullopt;
    std::lock_guard lock(state_->mutex);
    return state_->newer;
}

bool Checker::Done() const { return state_ && state_->done; }

}
