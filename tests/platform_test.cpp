#include "engine/platform/ed25519.h"
#include "engine/platform/http.h"
#include "engine/platform/os.h"
#include "engine/platform/signed_trailer.h"
#include "engine/platform/update_check.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--child-exit") {
        std::puts("hello");
        return std::atoi(argv[2]);
    }
    if (argc == 3 && std::string_view(argv[1]) == "--child-sleep") {
        std::this_thread::sleep_for(std::chrono::seconds(std::atoi(argv[2])));
        return 0;
    }
    int failed = 0;
    auto check = [&](bool ok, const char* label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); failed += !ok; };
    using namespace pt;
    check(http::ParseHttpDate("Tue, 06 Oct 2026 09:25:00 GMT") == 1791278700, "HTTP date");
    check(http::ParseHttpDate("Thu, 01 Jan 1970 00:00:00 GMT") == 0, "HTTP date epoch");
    check(!http::ParseHttpDate("not a date"), "HTTP date rejects text");
    check(update::CompareVersions("0.10.0", "0.9.2") > 0, "version order by number");
    check(update::CompareVersions("v0.2.0", "0.2.0") == 0, "leading v");
    check(update::CompareVersions("0.2.0-rc1", "0.2.0") < 0, "pre-release before release");
    check(update::CompareVersions("0.1", "0.1.0") == 0, "missing parts are zero");
    const char* manifest = R"({"version": "0.2.0", "notes": "Linux build", "url": "https://example.org/releases/latest",
        "platforms": {"windows": {"url": "https://example.org/setup.exe", "sha256": "00"}, "linux": {"url": "https://example.org/pt-linux.tar.gz"}},
        "published": 1791278700, "prerelease": false, "assets": [1, "two", {"three": 3}]})";
    const auto windows = update::ParseManifest(manifest, "windows");
    const auto lin = update::ParseManifest(manifest, "linux");
    const auto other = update::ParseManifest(manifest, "macos");
    check(windows && windows->version == "0.2.0" && windows->url == "https://example.org/setup.exe" && windows->notes == "Linux build", "manifest, windows");
    check(lin && lin->url == "https://example.org/pt-linux.tar.gz", "manifest, linux");
    check(other && other->url == "https://example.org/releases/latest", "manifest, other platform falls back to the release page");
    check(!update::ParseManifest(R"({"notes": "no version"})", "windows"), "manifest without a version");
    check(!update::ParseManifest("<html>", "windows"), "not JSON");
    check(!update::ParseManifest(R"({"version": "1", )", "windows"), "truncated JSON");
    const char* github = R"({"tag_name":"v1.0.2","html_url":"https://github.com/LoreanXavier/pt-pc/releases/tag/v1.0.2",
        "body":"Fixes\nMore details", "draft":false,"prerelease":false,
        "assets":[
          {"name":"P.T.PC.Port-portable-windows.zip","browser_download_url":"https://example.org/portable.zip"},
          {"name":"P.T.PC.Port.Setup-linux","browser_download_url":"https://example.org/linux"},
          {"name":"P.T.PC.Port-macOS-x64.zip","browser_download_url":"https://example.org/intel.zip"},
          {"name":"P.T.PC.Port.Setup.exe","browser_download_url":"https://example.org/windows.exe"},
          {"name":"P.T.PC.Port-macOS-arm64.zip","browser_download_url":"https://example.org/apple.zip"}
        ]})";
    const auto gh_windows = update::ParseManifest(github, "windows");
    const auto gh_linux = update::ParseManifest(github, "linux");
    const auto gh_arm = update::ParseManifest(github, "macos-arm64");
    const auto gh_intel = update::ParseManifest(github, "macos-x64");
    check(gh_windows && gh_windows->version == "v1.0.2" && gh_windows->url == "https://example.org/windows.exe",
          "GitHub release selects Windows installer instead of portable ZIP");
    check(gh_linux && gh_linux->url == "https://example.org/linux", "GitHub release selects Linux installer");
    check(gh_arm && gh_arm->url == "https://example.org/apple.zip", "GitHub release selects Apple silicon app");
    check(gh_intel && gh_intel->url == "https://example.org/intel.zip", "GitHub release selects Intel app");
    const char* no_asset = R"({"tag_name":"v1.0.2","html_url":"https://example.org/release","assets":[]})";
    const auto gh_fallback = update::ParseManifest(no_asset, "macos-arm64");
    check(gh_fallback && gh_fallback->url == "https://example.org/release", "missing platform asset falls back to release page");
    check(!update::ParseManifest(R"({"tag_name":"v1.0.2","html_url":"https://example.org/r","draft":true})", "windows"),
          "draft GitHub release is ignored");
    check(!update::ParseManifest(R"({"tag_name":"v1.0.2-rc1","html_url":"https://example.org/r","prerelease":true})", "windows"),
          "prerelease GitHub release is ignored");
    check(!update::ParseManifest(R"({"tag_name":"nightly","html_url":"https://example.org/r"})", "windows"),
          "non-version release tag is ignored");
    check(!update::ParseManifest(R"({"message":"API rate limit exceeded"})", "windows"), "API errors are not updates");
    check(!update::ParseManifest(R"({"tag_name":"v1.0.2.","html_url":"https://example.org/r"})", "windows"), "malformed version tag is ignored");
    const auto unsafe_asset = update::ParseManifest(R"({"tag_name":"v1.0.2","html_url":"https://example.org/r",
        "assets":[{"name":"P.T.PC.Port.Setup.exe","browser_download_url":"http://example.org/setup.exe"}]})", "windows");
    check(unsafe_asset && unsafe_asset->url == "https://example.org/r", "non-HTTPS asset is ignored");

    {
        auto bytes = [](const char* hex, size_t n) { return *ed25519::FromHex(hex, n); };
        const std::string seed = bytes("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", 32);
        const std::string rfc_public = bytes("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", 32);
        const std::string rfc_signature = bytes("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", 64);
        auto u8 = [](const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); };
        uint8_t pk[32], sig[64];
        ed25519::PublicKey(u8(seed), pk);
        ed25519::Sign(u8(seed), nullptr, 0, sig);
        check(std::memcmp(pk, rfc_public.data(), 32) == 0 && std::memcmp(sig, rfc_signature.data(), 64) == 0, "Ed25519 RFC 8032 test 1: key and signature");
        check(ed25519::Verify(u8(rfc_signature), nullptr, 0, u8(rfc_public)), "Ed25519 RFC 8032 test 1 verifies");
        uint8_t sha[64];
        ed25519::Sha512(reinterpret_cast<const uint8_t*>("abc"), 3, sha);
        check(ed25519::ToHex(sha, 64) == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
              "SHA-512 of abc");
        const std::string longer(200, 'x');
        ed25519::Sign(u8(seed), u8(longer), longer.size(), sig);
        check(ed25519::Verify(sig, u8(longer), longer.size(), pk), "Ed25519 sign and verify a longer message");

        const std::string key = std::string(32, '\x07');
        uint8_t release_like[32];
        ed25519::PublicKey(u8(key), release_like);
        const std::string public_key(reinterpret_cast<const char*>(release_like), 32);
        const std::string manifest = R"({"format":1,"product":"pt-pc-port","version":"1.0.4",)"
                                     R"("packages":{"windows":{"name":"P.T.PC.Port.Setup.exe","size":10,"sha256":")" + std::string(64, 'a') + R"("}},)"
                                     R"("files":{"windows":[{"path":"pt.exe","size":3,"sha256":")" + std::string(64, 'b') + R"("}]}})";
        auto sign = [&](const std::string& text) {
            uint8_t s[64];
            ed25519::Sign(u8(key), u8(text), text.size(), s);
            return ed25519::ToHex(s, 64);
        };
        const std::string good = sign(manifest);
        std::string error;
        const auto verified = update::VerifyManifest(manifest, good + "\n", public_key, &error);
        check(verified && verified->version == "1.0.4" && verified->packages.at("windows").size == 10 &&
                  verified->files.at("windows").size() == 1 && verified->files.at("windows")[0].path == "pt.exe",
              "signed manifest: good signature accepted and read");
        std::string tampered = manifest;
        tampered.replace(tampered.find("\"size\":10"), 9, "\"size\":11");
        check(!update::VerifyManifest(tampered, good, public_key, &error) && error.find("signature") != std::string::npos,
              "signed manifest: a changed manifest is refused");
        std::string bad_signature = good;
        bad_signature[10] = bad_signature[10] == '0' ? '1' : '0';
        check(!update::VerifyManifest(manifest, bad_signature, public_key), "signed manifest: a changed signature is refused");
        check(!update::VerifyManifest(manifest, good, rfc_public), "signed manifest: another key's verification fails (wrong key)");
        check(!update::VerifyManifest(manifest, "not a signature", public_key), "signed manifest: a signature file of text is refused");
        check(!update::VerifyManifest(manifest, good, public_key.substr(0, 31)), "signed manifest: a short key is refused");
        {
            auto raw = *ed25519::FromHex(good, 64);
            static const uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
            unsigned carry = 0;
            for (int i = 0; i < 32; ++i) {
                const unsigned sum = uint8_t(raw[32 + i]) + L[i] + carry;
                raw[32 + i] = char(sum & 0xff);
                carry = sum >> 8;
            }
            check(carry == 0 && !update::VerifyManifest(manifest, ed25519::ToHex(u8(raw), 64), public_key), "signed manifest: a non-canonical S is refused");
        }
        const std::string not_json = "{\"format\":1,";
        check(!update::VerifyManifest(not_json, sign(not_json), public_key), "signed manifest: signed but not valid JSON is refused");
        const std::string no_hash = R"({"format":1,"version":"1.0.4","packages":{"windows":{"name":"x","size":1}}})";
        check(!update::VerifyManifest(no_hash, sign(no_hash), public_key), "signed manifest: a package without its SHA-256 is refused");
        const std::string other_format = R"({"format":2,"version":"1.0.4","packages":{}})";
        check(!update::VerifyManifest(other_format, sign(other_format), public_key), "signed manifest: an unknown format is refused");
        check(update::ReleasePublicKey().size() == 32, "the embedded release public key is 32 bytes");
        auto files_manifest = [&](const std::string& extra) {
            return R"({"format":1,"version":"1.0.5","packages":{"windows":{"name":"P.T.PC.Port.Setup.exe","size":1000,"sha256":")" + std::string(64, 'a') +
                   R"("}},"files":{"windows":[{"path":"pt.exe","size":30,"sha256":")" + std::string(64, 'b') + "\"" + extra +
                   R"(},{"path":"shaders/a.spv","size":5,"sha256":")" + std::string(64, 'c') + R"(","offset":900,"packed":4}]}})";
        };
        const std::string ranged = files_manifest(R"(,"offset":100,"packed":20)");
        const auto with_ranges = update::VerifyManifest(ranged, sign(ranged), public_key, &error);
        check(with_ranges && with_ranges->files.at("windows")[0].offset == 100 && with_ranges->files.at("windows")[0].packed == 20,
              "signed manifest: a file's offset and packed size are read");
        const std::string outside = files_manifest(R"(,"offset":990,"packed":20)");
        check(!update::VerifyManifest(outside, sign(outside), public_key), "signed manifest: a file outside its package is refused");
        const std::string half = files_manifest(R"(,"offset":100)");
        check(!update::VerifyManifest(half, sign(half), public_key), "signed manifest: an offset without its packed size is refused");
        if (with_ranges) {
            const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("pt-estimate-" + std::to_string(pt::os::ProcessId()));
            std::filesystem::create_directories(dir / "shaders");
            std::ofstream(dir / "pt.exe", std::ios::binary) << std::string(30, 'x');
            std::ofstream(dir / "shaders" / "a.spv", std::ios::binary) << "12345";
            std::ofstream(dir / "pt-install-manifest.txt", std::ios::binary)
                << "pt-port-install 1\nversion=1.0.4\nfile=" << std::string(64, 'e') << " pt.exe\nfile=" << std::string(64, 'c') << " shaders/a.spv\n";
            check(update::EstimateDownload(*with_ranges, "windows", dir) == 20, "download estimate: only the changed file's packed bytes");
            std::filesystem::remove(dir / "shaders" / "a.spv");
            check(update::EstimateDownload(*with_ranges, "windows", dir) == 24, "download estimate: a missing file counts");
            auto big = *with_ranges;
            big.files.at("windows")[0].packed = 800;
            check(update::EstimateDownload(big, "windows", dir) == 1000, "download estimate: above 70% the whole package");
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    }
    {
        auto footer = [](uint64_t mo, uint64_t ml, uint64_t so, uint64_t sl, const char* magic = "PTSIGND1", uint32_t version = 1) {
            std::string f(48, '\0');
            std::memcpy(f.data(), magic, 8);
            std::memcpy(f.data() + 8, &version, 4);
            for (auto [at, v] : {std::pair{16, mo}, std::pair{24, ml}, std::pair{32, so}, std::pair{40, sl}}) std::memcpy(f.data() + at, &v, 8);
            return f;
        };
        auto parse = [](const std::string& f, uint64_t size) { return trailer::Parse(reinterpret_cast<const unsigned char*>(f.data()), size); };
        const auto good = parse(footer(1000, 200, 1200, 64), 1312);
        check(good && good->body() == 1000 && good->manifest_length == 200, "trailer: a footer is read");
        check(!parse(footer(1000, 200, 1200, 64), 1313), "trailer: a footer that does not end the file is refused");
        check(!parse(footer(1000, 200, 1201, 64), 1313), "trailer: a gap between manifest and signature is refused");
        check(!parse(footer(1000, 200, 1200, 63), 1311), "trailer: a signature of another size is refused");
        check(!parse(footer(1000, 200, 1200, 64, "PTSIGND2"), 1312) && !parse(footer(1000, 200, 1200, 64, "PTSIGND1", 2), 1312),
              "trailer: another magic or version is refused");
        check(!parse(footer(0, 0, 0, 64), 112), "trailer: an empty manifest is refused");
        const std::filesystem::path file = std::filesystem::temp_directory_path() / ("pt-trailer-" + std::to_string(pt::os::ProcessId()) + ".bin");
        {
            std::ofstream out(file, std::ios::binary);
            out << std::string(1000, 'b') << std::string(200, 'm') << std::string(64, 's') << footer(1000, 200, 1200, 64);
        }
        std::string manifest, signature;
        check(trailer::BodySize(file) == 1000 && trailer::ReadSigned(file, manifest, signature) && manifest == std::string(200, 'm') &&
                  signature == std::string(64, 's'),
              "trailer: the body size, the manifest and the signature of a file on disk");
        std::filesystem::remove(file);
    }
    {
        check(update::AllowedAssetUrl("https://github.com/LoreanXavier/pt-pc/releases/download/v1.0.4/P.T.PC.Port.Setup.exe"), "allow github.com");
        check(update::AllowedAssetUrl("https://objects.githubusercontent.com/github-production-release-asset/1"), "allow objects.githubusercontent.com");
        check(update::AllowedAssetUrl("https://release-assets.githubusercontent.com/github-production-release-asset/1?sp=r"), "allow release-assets");
        check(update::AllowedAssetUrl("https://GitHub.com/x"), "host compared without case");
        check(!update::AllowedAssetUrl("http://github.com/x"), "refuse plain http");
        check(!update::AllowedAssetUrl("https://github.com.example.org/x"), "refuse a look-alike host");
        check(!update::AllowedAssetUrl("https://example.org/github.com"), "refuse another host");
        check(!update::AllowedAssetUrl("https://user@github.com/x") && !update::AllowedAssetUrl("https://github.com@example.org/x"), "refuse user information");
        check(!update::AllowedAssetUrl("https://api.github.com/repos/x") && update::AllowedMetadataUrl("https://api.github.com/repos/LoreanXavier/pt-pc/releases/latest"),
              "api.github.com only for the release metadata");
        if (os::GetEnv("PT_UPDATE_TEST").empty())
            check(!update::AllowedAssetUrl("http://127.0.0.1:8000/x") && !update::AllowedAssetUrl("https://localhost/x"), "loopback refused outside tests");
        const char* signed_release = R"({"tag_name":"v1.0.4","html_url":"https://github.com/LoreanXavier/pt-pc/releases/tag/v1.0.4","assets":[
          {"name":"P.T.PC.Port.Setup.exe","size":416382464,"browser_download_url":"https://github.com/LoreanXavier/pt-pc/releases/download/v1.0.4/P.T.PC.Port.Setup.exe"},
          {"name":"P.T.PC.Port.Setup-linux","size":5,"browser_download_url":"https://example.org/P.T.PC.Port.Setup-linux"}]})";
        const auto release = update::ParseManifest(signed_release, "windows");
        check(release && release->package_url.ends_with("/P.T.PC.Port.Setup.exe") && release->package_size == 416382464 &&
                  release->page.ends_with("/v1.0.4") && !release->signed_update,
              "the in-game update's package and its size from the release assets");
        const auto linux_release = update::ParseManifest(signed_release, "linux");
        check(linux_release && linux_release->package_url.empty() && linux_release->package_size == 0,
              "release assets of the in-game update come only from allowed hosts");
        const auto mac = update::ParseManifest(signed_release, "macos-arm64");
        check(mac && mac->package_url.empty(), "macOS has no in-game update package");
        check(http::UrlHost("https://Release-Assets.githubusercontent.com:443/a") == "release-assets.githubusercontent.com" &&
                  http::UrlScheme("ftp://github.com/") .empty(),
              "URL host and scheme");
    }
    if (os::GetEnv("PT_UPDATE_MANIFEST_URL").empty()) {
#ifdef _WIN32
        _putenv_s("PT_UPDATE_MANIFEST_URL", "https://releases.invalid/pt-port/latest.json");
#else
        setenv("PT_UPDATE_MANIFEST_URL", "https://releases.invalid/pt-port/latest.json", 1);
#endif
        update::Checker placeholder;
        placeholder.Start();
        check(placeholder.Done() && !placeholder.Newer(), "the placeholder URL sends nothing");
#ifdef _WIN32
        _putenv_s("PT_UPDATE_MANIFEST_URL", "");
#else
        unsetenv("PT_UPDATE_MANIFEST_URL");
#endif
        update::Checker built_in;
        built_in.Start();
        for (int i = 0; i < 200 && !built_in.Done(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto newer = built_in.Newer();
        std::printf("update check at %s: %s\n", update::ManifestUrl().c_str(), newer ? (newer->version + " " + newer->url).c_str() : "nothing newer");
        check(built_in.Done(), "the built-in manifest address answers or times out");
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--network") {
        http::Request request;
        request.url = "https://www.google.com/generate_204";
        request.no_cache = true;
        const auto response = http::Get(request);
        check(response && response->status == 204 && response->date && *response->date > 1700000000, "HTTPS GET with a Date header");
        request.url = "https://api.github.com/repos/LoreanXavier/pt-pc/releases/latest";
        request.max_body = 512 * 1024;
        const auto api = http::Get(request);
        const auto published = api && api->status == 200 ? update::ParseManifest(api->body, update::Platform()) : std::nullopt;
        check(published && !published->version.empty() && published->url.starts_with("https://"), "real GitHub release API parses without a manifest asset");
        request.url = "https://nonexistent-host.invalid/";
        check(!http::Get(request), "an unreachable host is no answer");
        if (!os::GetEnv("PT_UPDATE_MANIFEST_URL").empty()) {
            update::Checker checker;
            checker.Start();
            for (int i = 0; i < 200 && !checker.Done(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const auto newer = checker.Newer();
            std::printf("update check: %s\n", newer ? (newer->version + " " + newer->url).c_str() : "nothing newer");
            check(checker.Done(), "update check finishes");
        }
    }

    check(os::ProcessAlive(os::ProcessId()) && !os::ProcessAlive(0xFFFFFFF0ull), "process alive check");
#ifdef _WIN32
    check(os::QuoteWindowsArgument(L"plain") == L"plain" && os::QuoteWindowsArgument(LR"(C:\Program Files\P.T. PC Port\)") == LR"("C:\Program Files\P.T. PC Port\\")" &&
              os::QuoteWindowsArgument(LR"(say "hi")") == LR"("say \"hi\"")" && os::QuoteWindowsArgument(L"") == LR"("")",
          "Windows argument quoting");
#endif
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("pt-platform-test-" + std::to_string(os::ProcessId()));
    std::filesystem::create_directories(dir);
    {
        os::FileLock first(dir / "lock");
        os::FileLock second(dir / "lock");
        check(first.Held() && !second.Held(), "file lock is exclusive");
    }
    {
        os::FileLock again(dir / "lock");
        check(again.Held(), "file lock is released");
    }
    {
        std::ofstream(dir / "data.bin", std::ios::binary) << "0123456789";
        FILE* f = os::OpenFile(dir / "data.bin", "rb");
        char c = 0;
        check(f && os::SeekFile(f, 7, SEEK_SET) == 0 && std::fread(&c, 1, 1, f) == 1 && c == '7', "open and 64-bit seek");
        if (f) std::fclose(f);
    }
    std::atomic<bool> cancel{false};
    const std::filesystem::path self = std::filesystem::absolute(argv[0]);
    const auto exit3 = os::RunProcess(self, {"--child-exit", "3"}, dir, dir / "out.log", cancel, std::chrono::seconds(10));
    const auto slow = os::RunProcess(self, {"--child-sleep", "30"}, dir, dir / "slow.log", cancel, std::chrono::milliseconds(500));
    std::ifstream log(dir / "out.log");
    std::string line;
    std::getline(log, line);
    check(exit3.started && exit3.exit_code == 3 && line.rfind("hello", 0) == 0, "process exit code and output");
    check(slow.started && slow.timed_out, "process timeout kills it");
    log.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::printf("%s\n", failed ? "platform test: FAIL" : "platform test: PASS");
    return failed ? 1 : 0;
}
