#include "engine/render/upscale/neural_rendering.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <vector>

#include "engine/core/ascii.h"
#include "engine/core/log.h"
#include "engine/core/resource_path.h"
#include "engine/platform/os.h"
#include "engine/platform/self_integrity.h"

namespace pt {

namespace {

constexpr const char* kKinds[] = {"off", "dlss5"};
constexpr const char* kStyles[] = {"default", "natural", "cinematic"};
constexpr const char* kRuntimeName = "nvngx_dlssnr.dll";

std::string Lower(const std::string& text) { return AsciiLowerCopy(text); }

std::string HashFile(const std::filesystem::path& path, uint64_t& size_out) {
    std::error_code ec;
    size_out = std::filesystem::file_size(path, ec);
    if (ec) {
        return {};
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    unsigned char digest[32];
    if (!integrity::Sha256(bytes.data(), bytes.size(), digest)) {
        return {};
    }
    static const char* hex = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    return out;
}

std::filesystem::path RuntimeIn(const std::filesystem::path& where, int depth) {
    std::error_code ec;
    if (where.empty() || !std::filesystem::exists(where, ec)) {
        return {};
    }
    if (std::filesystem::is_regular_file(where, ec)) {
        return AsciiLowerCopy(where.filename().string()) == kRuntimeName ? where : std::filesystem::path();
    }
    const std::filesystem::path direct = where / kRuntimeName;
    if (std::filesystem::exists(direct, ec)) {
        return direct;
    }
    if (depth <= 0) {
        return {};
    }
    std::filesystem::path best;
    std::filesystem::file_time_type best_time{};
    std::filesystem::recursive_directory_iterator it(where, std::filesystem::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (it.depth() >= depth) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && AsciiLowerCopy(it->path().filename().string()) == kRuntimeName) {
            const auto t = std::filesystem::last_write_time(it->path(), ec);
            if (best.empty() || t > best_time) {
                best = it->path();
                best_time = t;
            }
        }
    }
    return best;
}

std::filesystem::path EnvPath(const char* name) {
    const std::string value = os::GetEnv(name);
    return value.empty() ? std::filesystem::path() : std::filesystem::path(value);
}

}

const char* NeuralRenderingKey(NeuralRenderingKind kind) {
    const int i = static_cast<int>(kind);
    return i >= 0 && i < static_cast<int>(NeuralRenderingKind::Count) ? kKinds[i] : "off";
}

bool ParseNeuralRendering(const std::string& text, NeuralRenderingKind& out) {
    const std::string key = Lower(text);
    for (int i = 0; i < static_cast<int>(NeuralRenderingKind::Count); ++i) {
        if (key == kKinds[i]) {
            out = static_cast<NeuralRenderingKind>(i);
            return true;
        }
    }
    out = NeuralRenderingKind::Off;
    return false;
}

const char* NeuralStyleKey(NeuralStyle style) {
    const int i = static_cast<int>(style);
    return i >= 0 && i < static_cast<int>(NeuralStyle::Count) ? kStyles[i] : "default";
}

bool ParseNeuralStyle(const std::string& text, NeuralStyle& out) {
    const std::string key = Lower(text);
    for (int i = 0; i < static_cast<int>(NeuralStyle::Count); ++i) {
        if (key == kStyles[i]) {
            out = static_cast<NeuralStyle>(i);
            return true;
        }
    }
    out = NeuralStyle::Default;
    return false;
}

bool NeuralRenderingVendorSupported(uint32_t vendor_id) {
    return vendor_id == 0x10DE;
}

std::optional<NeuralRuntimeInfo> FindNeuralRuntime(const std::string& ini_path) {
    struct Candidate {
        std::filesystem::path where;
        int depth;
        std::string source;
    };
    std::vector<Candidate> candidates;
    candidates.push_back({ExecutableDir(), 0, "next to pt.exe"});
    if (!ini_path.empty()) {
        candidates.push_back({std::filesystem::path(ini_path), 2, "pt.ini upscaling.neural_runtime"});
    }
    const std::filesystem::path local = EnvPath("LOCALAPPDATA");
    const std::filesystem::path home = EnvPath("USERPROFILE");
    const std::filesystem::path appdata = EnvPath("APPDATA");
    if (!local.empty()) {
        candidates.push_back({local / "Programs" / "DLSS 5 Swapper", 4, "DLSS 5 Swapper"});
        candidates.push_back({local / "rtx-unlock", 3, "rtx-unlock"});
        candidates.push_back({local / "DLSS5-Autopilot", 3, "DLSS5-Autopilot"});
        candidates.push_back({local / "dlss5oneclick", 3, "DLSS5oneclick"});
        candidates.push_back({local / "DLSS5-Feeder", 3, "DLSS5-Feeder"});
    }
    if (!home.empty()) {
        candidates.push_back({home / "Videos" / "DLSS5 Player", 2, "DLSS5 Player"});
    }
    if (!appdata.empty()) {
        candidates.push_back({appdata / "rtx-unlock", 3, "rtx-unlock"});
    }
    if (const std::filesystem::path env = EnvPath("PT_NEURAL_RUNTIME"); !env.empty()) {
        candidates.insert(candidates.begin(), {env, 2, "PT_NEURAL_RUNTIME"});
    }
    for (const Candidate& c : candidates) {
        const std::filesystem::path found = RuntimeIn(c.where, c.depth);
        if (found.empty()) {
            continue;
        }
        NeuralRuntimeInfo info;
        info.path = found;
        info.sha256 = HashFile(found, info.size);
        info.source = c.source;
        return info;
    }
    return std::nullopt;
}

}
