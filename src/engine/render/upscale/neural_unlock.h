#pragma once

#include <atomic>
#include <functional>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pt::nr_unlock {

inline constexpr std::string_view kToolName = "DLSS5-Autopilot";
inline constexpr std::string_view kToolVersion = "2.0.8";
inline constexpr std::string_view kToolUrl =
    "https://github.com/Kizzuwatnaa/DLSS5-Autopilot/releases/download/v2.0.8/DLSS5-Autopilot-v2.0.8-win64.zip";
inline constexpr uint64_t kToolZipSize = 13071075;
inline constexpr std::string_view kToolZipSha256 = "c16596b240ad53b155baffaf2e33517de567765429d3ea264229ef500278b000";
inline constexpr std::string_view kToolExe = "dlss5-autopilot.exe";
inline constexpr uint64_t kToolExeSize = 13322471;
inline constexpr std::string_view kToolExeSha256 = "8c53daa84c6f6cd7d51abff2992d8420071ae8f200898d0df1b2f1d8751848e4";
inline constexpr std::string_view kToolLatestApi = "https://api.github.com/repos/Kizzuwatnaa/DLSS5-Autopilot/releases/latest";
inline constexpr std::string_view kToolSumsExePath = "dist/dlss5-autopilot.exe";
struct ToolSource {
    enum class Which { Pinned, Latest } which = Which::Pinned;
    std::string tag;
    std::string zip_sha;
    std::string note;
};
inline constexpr std::string_view kRoute = "native";
inline constexpr std::string_view kManifest = "dlss5-autopilot.json";
inline constexpr std::string_view kAddonSection = "RenoDX.DLSS5";
inline constexpr std::string_view kParkedAddonPath = ".\\pt-addons-parked\\";
inline constexpr std::string_view kOwnReShadeDir = "reshade";
inline constexpr std::string_view kOwnReShadeFile = "dxgi.dll";
inline constexpr float kPaperWhite = 1.0f;
inline constexpr float kLocalTone = 0.0f;

enum class GpuClass { Other, NeedsUnlock, Native };
GpuClass ClassifyGpu(uint32_t vendor_id, uint32_t device_id, std::string_view adapter);

struct Install {
    bool complete = false;
    std::string route;
    std::string tool;
    std::string reshade;
    std::string addon;
    std::string runtime;
    std::vector<std::string> files;
};
std::optional<Install> ParseInstall(std::string_view json);
std::optional<Install> ReadInstall(const std::filesystem::path& game_dir);
bool Usable(const Install& install);

struct AddonSettings {
    bool on = false;
    int style = 0;
    float intensity = 1.0f;
    bool parked = false;
    std::vector<std::pair<std::string, std::string>> extra;
};
std::vector<std::pair<std::string, std::string>> ParseAddonKeys(std::string_view text);
std::string ApplyAddonSettings(std::string_view ini_text, const AddonSettings& settings);
bool WriteAddonSettings(const std::filesystem::path& game_dir, const AddonSettings& settings, std::string* error = nullptr);

struct RouteReport {
    enum class State { Unknown, Off, Running, Failed } state = State::Unknown;
    std::string code;
    std::string line;
};
RouteReport ParseReShadeLog(std::string_view text);
RouteReport ReadRouteReport(const std::filesystem::path& game_dir);

bool LoadDxgiForRoute(const std::filesystem::path& game_dir, bool install_present, bool load);
bool SameFolder(const std::filesystem::path& a, const std::filesystem::path& b);

inline constexpr int kNrToggleVk = 0x0B;
inline constexpr int kNrScreenshotVk = 0x0A;
bool InstallNrToggleHook();
bool NrToggleHookReady();
bool PressNrToggle();
uint32_t NrTogglesStarted();
uint32_t NrTogglesDelivered();

class Job {
public:
    enum class Kind { Install, Remove };
    enum class Phase { Idle, Downloading, Ready, Failed };
    struct Status {
        Kind kind = Kind::Install;
        Phase phase = Phase::Idle;
        int percent = 0;
        std::string failure;
        std::string detail;
        uint32_t serial = 0;
    };
    bool Start(Kind kind, const std::filesystem::path& work_dir);
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

std::filesystem::path ToolPath(const std::filesystem::path& work_dir);
bool PrepareTool(const std::filesystem::path& work_dir, const std::function<void(int)>& percent, const std::function<bool()>& cancelled,
                 std::string* failure_key, std::string* detail);
inline constexpr std::string_view kResultFile = "route-result.txt";
std::vector<std::string> TopLevelNames(const std::filesystem::path& dir);

std::string FileSha256(const std::filesystem::path& path);

bool IsOwnReShade(const std::filesystem::path& game_dir);

}
