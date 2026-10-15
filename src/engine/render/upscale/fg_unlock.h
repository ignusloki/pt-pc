#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pt::fg_unlock {

inline constexpr std::string_view kToolName = "dlssg_for_sm86";
inline constexpr std::string_view kToolVersion = "0.4.0";
inline constexpr std::string_view kToolUrl =
    "https://github.com/sdli1995/dlssg_for_sm86/releases/download/0.4.0/dlssg-release-0.4.0-x64.zip";
inline constexpr uint64_t kToolZipSize = 80551324;
inline constexpr std::string_view kToolZipSha256 = "5865360c3295038f0ff979d303bb7eb9437eec09fbc08686967d3b5d41d9c019";
inline constexpr std::string_view kToolLatestApi = "https://api.github.com/repos/sdli1995/dlssg_for_sm86/releases/latest";
inline constexpr std::string_view kIni = "dlssg_sm86.ini";
inline constexpr std::string_view kDir = "dlssg_sm86";
inline constexpr std::string_view kPackageManifest = "manifest.json";
inline constexpr std::string_view kProxyMarker = "dlssg_sm86";
inline constexpr const char* kProxyCandidates[] = {"version.dll", "winmm.dll", "dbghelp.dll"};
inline constexpr std::string_view kDlss5Proxy = "dxgi.dll";
inline constexpr std::string_view kManifest = "pt-dlssg-route.json";
inline constexpr std::string_view kResultFile = "fg-route-result.txt";

struct ToolSource {
    enum class Which { Pinned, Latest } which = Which::Pinned;
    std::string tag;
    std::string zip_sha;
    std::string note;
};

bool Applicable(uint32_t vendor_id, std::string_view dlssg_note);

struct Install {
    bool complete = false;
    std::string tool_tag;
    std::string runtime;
    std::string proxy;
    std::vector<std::string> files;
};
std::string ChooseProxy(const std::filesystem::path& game_dir, std::string_view keep);
std::optional<Install> ParseInstall(std::string_view json);
std::optional<Install> ReadInstall(const std::filesystem::path& game_dir);
bool Usable(const Install& install);
bool ProxyPresent(const std::filesystem::path& game_dir);

struct RouteReport {
    enum class State { Unknown, Active, Fallback } state = State::Unknown;
    std::string line;
};
RouteReport ReadRouteReport(const std::filesystem::path& game_dir);

std::string FileSha256(const std::filesystem::path& path);
std::filesystem::path ZipPath(const std::filesystem::path& work_dir);
bool PrepareTool(const std::filesystem::path& work_dir, const std::function<void(int)>& percent, const std::function<bool()>& cancelled,
                 std::string* failure_key, std::string* detail);

class Job {
public:
    enum class Kind { Install, Remove, Reinstall };
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

}
