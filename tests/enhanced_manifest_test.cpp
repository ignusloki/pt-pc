#include "engine/assets/enhanced_textures.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/platform/os.h"

namespace {

int failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failed += !ok;
}

void Write(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

}

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("pt_manifest_test_" + std::to_string(pt::os::ProcessId()));
    const fs::path game = root / "game", cache = root / "cache", runtime = root / "runtime";
    fs::remove_all(root);
#ifdef _WIN32
    const char* exe = "realesrgan-ncnn-vulkan.exe";
#else
    const char* exe = "realesrgan-ncnn-vulkan";
#endif
    Write(game / "texture.qar", "an archive of some bytes");
    Write(runtime / "models" / "realesr-general-x4v3-x2.param", "param");
    Write(runtime / "models" / "realesr-general-x4v3-x2.bin", "weights");
    Write(runtime / exe, "upscaler");
    const uint64_t model = pt::EnhancedModelKey(runtime);
    Check(model != 0, "the fake runtime has a model key");

    std::error_code ec;
    const auto bytes = fs::file_size(game / "texture.qar", ec);
    const auto mtime = static_cast<long long>(fs::last_write_time(game / "texture.qar").time_since_epoch().count());
    auto manifest = [&](const std::string& kinds, const std::string& size_cap, const std::string& archive_bytes) {
        return "cache_version=3\nkinds=" + kinds + "\nfilter_versions=2,1,1\nmodel=realesr-general-x4v3-x2\nmodel_fingerprint=" + std::to_string(model) +
               "\narchive_bytes=" + archive_bytes + "\narchive_mtime=" + std::to_string(mtime) + "\ntextures=114\nmax_output=" + size_cap + "\n";
    };
    Check(!pt::EnhancedManifestCurrent(game, cache, runtime, 7, 0), "no manifest, an incomplete cache");
    Write(cache / "manifest.txt", manifest("7", "0", std::to_string(bytes)));
    Check(pt::EnhancedManifestCurrent(game, cache, runtime, 7, 0), "a manifest for this cache is current");
    Check(!pt::EnhancedManifestCurrent(game, cache, runtime, 1, 0), "other kinds: not current");
    Check(!pt::EnhancedManifestCurrent(game, cache, runtime, 7, 2048), "another size cap: not current");
    Check(!pt::EnhancedManifestCurrent(game, cache, cache / "missing", 7, 0), "no model: not current");
    Write(cache / "manifest.txt", manifest("7", "0", std::to_string(bytes + 1)));
    Check(!pt::EnhancedManifestCurrent(game, cache, runtime, 7, 0), "another archive: not current");
    Write(runtime / "models" / "realesr-general-x4v3-x2.bin", "other weights");
    Write(cache / "manifest.txt", manifest("7", "0", std::to_string(bytes)));
    Check(!pt::EnhancedManifestCurrent(game, cache, runtime, 7, 0), "another model: not current");
    fs::remove_all(root, ec);
    return failed ? 1 : 0;
}
