#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pt::update {

std::string ManifestUrl();
std::string_view CurrentVersion();
std::string_view Platform();

struct Release {
    std::string version;
    std::string url;
    std::string notes;
    std::string page;
    std::string package_url;
    uint64_t package_size = 0;
    bool signed_update = false;
    uint64_t download_bytes = 0;
};

int CompareVersions(std::string_view a, std::string_view b);
std::optional<Release> ParseManifest(std::string_view json, std::string_view platform);

std::string_view PackageAsset(std::string_view platform);
struct PackageEntry {
    std::string name;
    uint64_t size = 0;
    std::string sha256;
};
struct FileEntry {
    std::string path;
    uint64_t size = 0;
    std::string sha256;
    uint64_t offset = 0;
    uint64_t packed = 0;
};
struct UpdateManifest {
    std::string version;
    std::map<std::string, PackageEntry> packages;
    std::map<std::string, std::vector<FileEntry>> files;
};
std::string ReleasePublicKey();
std::optional<UpdateManifest> VerifyManifest(std::string_view manifest, std::string_view signature_text, std::string_view public_key,
                                             std::string* error = nullptr);
bool AllowedAssetUrl(std::string_view url);
bool AllowedMetadataUrl(std::string_view url);
bool TestOverrides();
std::optional<UpdateManifest> FetchSignedManifest(const Release& release, std::string* error, bool* range_ignored = nullptr);
std::optional<UpdateManifest> ReadSignedManifest(const Release& release, const std::filesystem::path& package, std::string* error);
inline constexpr double kRangeShare = 0.7;
uint64_t EstimateDownload(const UpdateManifest& manifest, std::string_view platform, const std::filesystem::path& install);

class Checker {
public:
    void Start(const std::filesystem::path& install = {});
    void Fake(std::string_view version);
    std::optional<Release> Newer() const;
    bool Done() const;

private:
    struct State {
        std::mutex mutex;
        std::optional<Release> newer;
        std::atomic<bool> done{false};
    };
    std::shared_ptr<State> state_;
};

}
