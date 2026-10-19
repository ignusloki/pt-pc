#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pt::nr_native {

inline constexpr std::string_view kRuntimeVersion = "310.9.16";
inline constexpr std::string_view kRuntimeUrl =
    "https://github.com/LoreanXavier/pt-dlss5-runtime/releases/download/dlssnr-310.9.16/nvngx_dlssnr.dll";
inline constexpr uint64_t kRuntimeSize = 266424944;
inline constexpr std::string_view kRuntimeSha256 = "2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f";
inline constexpr std::string_view kRuntimeFile = "nvngx_dlssnr.dll";
inline constexpr std::string_view kCodeUrl =
    "https://github.com/RankFTW/rhi-repo/releases/download/dlssnr-310.8.SF-v2/nvngx_dlssnr_310.8.SF-v2.zip";
inline constexpr uint64_t kCodeZipSize = 116693212;
inline constexpr std::string_view kCodeZipSha256 = "1da35941894994eb087e017577829e492454e9bae3a6a9397027069ceb74955c";
inline constexpr std::string_view kCodeDllSha256 = "6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927";
inline constexpr size_t kBundles = 15;
inline constexpr std::string_view kFolder = "dlss5-native";
inline constexpr std::string_view kRecord = "install.json";
inline constexpr std::string_view kRemoveMarker = "remove-at-start";
inline constexpr std::string_view kCompanion = "pt-native-nvngx.dll";

int CodeArchFor(uint32_t vendor_id, uint32_t device_id, std::string_view adapter);

struct Install {
    std::filesystem::path dir;
    std::filesystem::path runtime;
    std::filesystem::path code;
    int arch = 0;
    std::string version;
};
std::optional<Install> Read(const std::filesystem::path& data_dir, int arch);

void Startup(const std::filesystem::path& data_dir);
bool UseCard(int arch);
const std::optional<Install>& Active();
bool Enabled();
std::filesystem::path CompanionPath();
std::filesystem::path CodePath();

struct Bundle {
    size_t offset = 0, size = 0;
    struct Entry { uint16_t kind = 0; uint32_t arch = 0; size_t offset = 0, size = 0; uint64_t unpacked = 0; };
    std::vector<Entry> entries;
};
std::vector<Bundle> FindBundles(std::span<const uint8_t> dll);
uint64_t BundleHash(std::span<const uint8_t> bytes);
bool ExtractCode(std::span<const uint8_t> runtime_dll, std::span<const uint8_t> code_dll, int arch, const std::filesystem::path& out,
                 std::string* detail);

bool InstallTo(const std::filesystem::path& data_dir, int arch, const std::function<void(int)>& percent,
               const std::function<bool()>& cancelled, std::string* failure_key, std::string* detail);
bool RemoveFrom(const std::filesystem::path& data_dir, bool* deferred, std::string* detail);

class Job {
public:
    enum class Kind { Install, Reinstall, Remove };
    enum class Phase { Idle, Working, Done, Failed };
    struct Status {
        Kind kind = Kind::Install;
        Phase phase = Phase::Idle;
        int percent = 0;
        bool deferred = false;
        std::string failure;
        std::string detail;
        uint32_t serial = 0;
    };
    bool Start(Kind kind, const std::filesystem::path& data_dir, int arch);
    Status Get() const;
    bool Busy() const;

private:
    struct State {
        mutable std::mutex mutex;
        Status status;
        std::atomic<bool> busy{false};
        std::atomic<bool> cancel{false};
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
};

}
