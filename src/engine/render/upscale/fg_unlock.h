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

inline constexpr std::string_view kPinnedProxySha256 = "7b3f90435be4f94348bf8d128c84871c7f904c704f7690601a263277c327a7b2";
inline constexpr uint64_t kPinnedProxySize = 853280;
inline constexpr std::string_view kRetiredDir = "retired-fg-proxy";

struct ToolSource {
    enum class Which { Pinned, Latest } which = Which::Pinned;
    std::string tag;
    std::string zip_sha;
    std::string note;
};

inline constexpr bool Applicable(int code_arch, std::string_view dlssg_note, bool installed = false) {
    return (code_arch == 75 || code_arch == 86) && (installed || dlssg_note == "pc_note_dlssg_gpu");
}

struct Install {
    bool complete = false;
    std::string tool_tag;
    std::string runtime;
    std::string proxy;
    std::vector<std::string> files;
    std::string proxy_sha256;
};
std::string ChooseProxy(const std::filesystem::path& game_dir, std::string_view keep);
std::optional<Install> ParseInstall(std::string_view json);
std::optional<Install> ReadInstall(const std::filesystem::path& game_dir);
bool Usable(const Install& install);
bool ProxyPresent(const std::filesystem::path& game_dir);

bool RecordedProxyPresent(const std::filesystem::path& game_dir);

struct ProxyFile {
    std::string name;
    uint64_t size = 0;
    std::string sha256;
    bool marker = false;
    bool loaded = false;
};
std::vector<ProxyFile> UnrecordedProxies(const std::filesystem::path& game_dir);
std::string DescribeUnrecorded(const std::vector<ProxyFile>& files);
std::string RetireShippedProxy(const std::filesystem::path& game_dir, const std::filesystem::path& archive_dir);
struct ProxyRepair {
    enum class Result { NoRecord, Fine, Restored, NeedsReinstall } result = Result::NoRecord;
    std::string detail;
};
ProxyRepair RepairRecordedProxy(const std::filesystem::path& game_dir, const std::filesystem::path& work_dir,
                                const std::vector<std::filesystem::path>& sources, const std::filesystem::path& archive_dir);

struct RouteReport {
    enum class State { Unknown, Active, Fallback } state = State::Unknown;
    std::string line;
};
RouteReport ReadRouteReport(const std::filesystem::path& game_dir);

std::string FileSha256(const std::filesystem::path& path);
std::filesystem::path ZipPath(const std::filesystem::path& work_dir);
bool PrepareTool(const std::filesystem::path& work_dir, const std::function<void(int)>& percent, const std::function<bool()>& cancelled,
                 std::string* failure_key, std::string* detail);

bool InstallLive(const std::filesystem::path& work_dir, const std::filesystem::path& game_dir, std::string& detail,
                 const std::atomic<bool>* cancelled = nullptr);
bool LoadLiveProxy(const std::filesystem::path& game_dir, std::string& detail);

class Job {
public:
    ~Job() { Cancel(); }
    void Cancel() { state_->cancel = true; }
    enum class Kind { Install, Remove, Reinstall };
    enum class Phase { Idle, Downloading, Ready, Failed };
    struct Status {
        Kind kind = Kind::Install;
        Phase phase = Phase::Idle;
        int percent = 0;
        std::string failure;
        std::string detail;
        uint32_t serial = 0;
        bool installed_live = false;
    };
    bool Start(Kind kind, const std::filesystem::path& work_dir, const std::filesystem::path& live_install_dir = {});
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
