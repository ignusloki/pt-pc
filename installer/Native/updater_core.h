#pragma once
#include "setup_core.h"

#include <chrono>
#include <cstring>
#include <set>
#include <thread>

#include "engine/platform/http.h"
#include "engine/platform/signed_trailer.h"
#include "engine/platform/update_check.h"

namespace pt::setup {

enum class UpdatePhase { Waiting, Checking, Downloading, Verifying, Installing, Starting };

struct GameUpdatePlatform {
    std::string unique_id;
    uint64_t pid = 0;
    std::function<void(const fs::path&)> check_parents;
    std::function<std::string(const fs::path&)> hash_file;
    std::function<std::string(const fs::path&)> hash_package;
    std::function<std::pair<const unsigned char*, size_t>(const fs::path& package)> open_payload;
    std::function<void()> close_payload;
    std::function<void(const fs::path&)> after_unpack;
    std::function<void(const std::vector<InstalledFile>&)> validate_runtime;
    std::function<bool(uint64_t)> process_alive;
    std::function<std::vector<fs::path>()> known_folders;
    std::function<void(UpdatePhase)> phase;
    int fail_at = -1;
};

struct GameUpdateResult {
    std::string version;
    InstallOutcome outcome;
    size_t needed = 0;
    uint64_t downloaded = 0;
    int requests = 0;
    bool ranged = false;
};

inline std::string StripV(std::string version) {
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
    return version;
}

inline constexpr uint64_t kRangeGap = 256 * 1024;
struct RangeFetch {
    uint64_t bytes = 0;
    int requests = 0;
    bool range_ignored = false;
};
class RunWriter {
public:
    RunWriter(const fs::path& folder, const std::vector<const update::FileEntry*>& files, uint64_t begin) : folder_(folder), files_(files), pos_(begin) {}
    ~RunWriter() { Close(); }
    bool Write(const char* data, size_t size) {
        while (size && error_.empty()) {
            if (index_ >= files_.size()) return true;
            const update::FileEntry& file = *files_[index_];
            if (pos_ < file.offset) {
                const uint64_t skip = std::min<uint64_t>(size, file.offset - pos_);
                pos_ += skip;
                data += skip;
                size -= size_t(skip);
                continue;
            }
            if (!open_ && !Open(file)) return false;
            const size_t take = size_t(std::min<uint64_t>(size, file.offset + file.packed - pos_));
            if (!Inflate(file, data, take)) return false;
            pos_ += take;
            data += take;
            size -= take;
            if (pos_ == file.offset + file.packed) {
                if (!ended_ || written_ != file.size) return Fail("the data of " + file.path + " is incomplete");
                Close();
                if (!out_ok_) return Fail("could not write " + file.path);
                ++index_;
            }
        }
        return error_.empty();
    }
    bool Complete() const { return error_.empty() && index_ == files_.size(); }
    const std::string& Error() const { return error_; }

private:
    bool Fail(std::string why) {
        if (error_.empty()) error_ = std::move(why);
        Close();
        return false;
    }
    bool Open(const update::FileEntry& file) {
        fs::path target;
        try {
            target = Contained(folder_, file.path);
        } catch (const std::exception&) {
            return Fail("unsafe path " + file.path);
        }
        std::error_code error;
        fs::create_directories(target.parent_path(), error);
        out_.open(target, std::ios::binary | std::ios::trunc);
        if (!out_) return Fail("could not create " + file.path);
        std::memset(&z_, 0, sizeof(z_));
        if (inflateInit(&z_) != Z_OK) return Fail("zlib failed");
        open_ = true;
        ended_ = false;
        out_ok_ = true;
        written_ = 0;
        return true;
    }
    bool Inflate(const update::FileEntry& file, const char* data, size_t size) {
        z_.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
        z_.avail_in = uInt(size);
        unsigned char buffer[64 * 1024];
        while (z_.avail_in > 0 || !ended_) {
            if (ended_) return Fail("the data of " + file.path + " runs on past its end");
            z_.next_out = buffer;
            z_.avail_out = sizeof(buffer);
            const int result = inflate(&z_, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR) return Fail("the data of " + file.path + " is damaged");
            const size_t produced = sizeof(buffer) - z_.avail_out;
            written_ += produced;
            if (written_ > file.size) return Fail("the data of " + file.path + " is larger than its size");
            if (produced) {
                out_.write(reinterpret_cast<const char*>(buffer), std::streamsize(produced));
                progress.Add(produced);
            }
            if (result == Z_STREAM_END) ended_ = true;
            if (z_.avail_in == 0 && z_.avail_out != 0) break;
        }
        return true;
    }
    void Close() {
        if (!open_) return;
        inflateEnd(&z_);
        out_.close();
        out_ok_ = bool(out_);
        open_ = false;
    }
    fs::path folder_;
    std::vector<const update::FileEntry*> files_;
    uint64_t pos_;
    size_t index_ = 0;
    z_stream z_{};
    std::ofstream out_;
    bool open_ = false, ended_ = false, out_ok_ = true;
    uint64_t written_ = 0;
    std::string error_;
};
inline RangeFetch FetchRanges(const std::string& package_url, const std::vector<update::FileEntry>& wanted, const fs::path& folder,
                              const std::function<std::string(const fs::path&)>& hash_file) {
    RangeFetch stats;
    std::string url = package_url;
    auto fetch = [&](std::vector<const update::FileEntry*> files) -> bool {
        std::sort(files.begin(), files.end(), [](const auto* a, const auto* b) { return a->offset < b->offset; });
        for (size_t first = 0; first < files.size();) {
            size_t last = first;
            uint64_t end = files[first]->offset + files[first]->packed;
            while (last + 1 < files.size() && files[last + 1]->offset >= end && files[last + 1]->offset - end <= kRangeGap) {
                ++last;
                end = std::max(end, files[last]->offset + files[last]->packed);
            }
            const uint64_t begin = files[first]->offset;
            const std::vector<const update::FileEntry*> run(files.begin() + std::ptrdiff_t(first), files.begin() + std::ptrdiff_t(last) + 1);
            for (int attempt = 0;; ++attempt) {
                CheckCancel();
                RunWriter writer(folder, run, begin);
                http::FetchRequest get;
                get.url = url;
                get.timeout_ms = 30000;
                get.max_bytes = end - begin;
                get.range = "bytes=" + std::to_string(begin) + "-" + std::to_string(end - 1);
                get.allow = [](const std::string& u) { return update::AllowedAssetUrl(u); };
                get.write = [&](const char* data, size_t size) { return writer.Write(data, size); };
                get.cancelled = [] { return hooks.cancelled && hooks.cancelled(); };
                const auto fetched = http::Fetch(get);
                ++stats.requests;
                stats.bytes += fetched.bytes;
                if (fetched.range_ignored) {
                    stats.range_ignored = true;
                    return false;
                }
                if (fetched.ok && writer.Complete()) {
                    if (!fetched.final_url.empty()) url = fetched.final_url;
                    break;
                }
                if (attempt == 0 && writer.Error().empty()) {
                    url = package_url;
                    continue;
                }
                throw std::runtime_error("The download of the changed files failed: " + (writer.Error().empty() ? fetched.error : writer.Error()) + ".");
            }
            first = last + 1;
        }
        return true;
    };
    std::vector<const update::FileEntry*> all;
    for (const auto& file : wanted) all.push_back(&file);
    if (!fetch(all)) return stats;
    auto wrong = [&](const std::vector<const update::FileEntry*>& files) {
        std::vector<const update::FileEntry*> bad;
        for (const auto* file : files) {
            std::string digest;
            try {
                digest = hash_file(Contained(folder, file->path));
            } catch (const std::exception&) {
            }
            if (digest != file->sha256) bad.push_back(file);
        }
        return bad;
    };
    auto bad = wrong(all);
    if (!bad.empty()) {
        if (!fetch(bad)) return stats;
        bad = wrong(bad);
        if (!bad.empty()) throw std::runtime_error("The downloaded " + bad.front()->path + " does not match the signed manifest (SHA-256). Nothing was changed.");
    }
    return stats;
}

inline std::string RangeFetchTest(const std::string& url, const fs::path& index, const fs::path& folder, const std::function<std::string(const fs::path&)>& hash_file) {
    std::vector<update::FileEntry> files;
    std::ifstream in(index, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        update::FileEntry file;
        size_t at = 0;
        auto field = [&] {
            const size_t space = line.find(' ', at);
            std::string text = line.substr(at, space - at);
            at = space == std::string::npos ? line.size() : space + 1;
            return text;
        };
        file.offset = std::strtoull(field().c_str(), nullptr, 10);
        file.packed = std::strtoull(field().c_str(), nullptr, 10);
        file.size = std::strtoull(field().c_str(), nullptr, 10);
        file.sha256 = field();
        file.path = line.substr(at);
        if (!file.packed || file.path.empty()) continue;
        files.push_back(std::move(file));
    }
    if (files.empty()) throw std::runtime_error("no files in the index");
    uint64_t packed = 0;
    for (const auto& file : files) packed += file.packed;
    progress.Begin(1);
    const RangeFetch fetched = FetchRanges(url, files, folder, hash_file);
    if (fetched.range_ignored) throw std::runtime_error("the server ignored Range");
    return "PASS " + std::to_string(files.size()) + " files checked against their SHA-256; " + std::to_string(fetched.requests) + " requests, " +
           std::to_string(fetched.bytes) + " bytes for " + std::to_string(packed) + " packed bytes";
}

inline GameUpdateResult ApplyGameUpdate(const fs::path& install_in, const GameUpdatePlatform& platform, std::string& page) {
    auto phase = [&](UpdatePhase p) { if (platform.phase) platform.phase(p); };
    const fs::path install = FolderPath(install_in);
    if (platform.check_parents) platform.check_parents(install);
    {
        std::error_code e;
        if (fs::exists(JournalFile(install), e)) RecoverEarlierUpdate(install, platform.process_alive);
    }
    const ExistingInstall old = InspectInstall(install);
    if (!old.found || !old.has_manifest)
        throw std::runtime_error("This folder is not a P.T. PC Port installed by its setup; download the new version from the release page.");
    const std::string installed = old.version.empty() ? std::string(update::CurrentVersion()) : old.version;
    const auto& hash_package = platform.hash_package ? platform.hash_package : platform.hash_file;

    phase(UpdatePhase::Checking);
    const std::string endpoint = update::ManifestUrl();
    if (!update::AllowedMetadataUrl(endpoint)) throw std::runtime_error("The update address " + endpoint + " is not allowed.");
    std::string body;
    http::FetchRequest meta;
    meta.url = endpoint;
    meta.timeout_ms = 15000;
    meta.max_bytes = 512 * 1024;
    meta.allow = [](const std::string& url) { return update::AllowedMetadataUrl(url); };
    meta.write = [&](const char* data, size_t size) { body.append(data, size); return true; };
    if (const auto fetched = http::Fetch(meta); !fetched.ok) throw std::runtime_error("Could not reach the release page: " + fetched.error + ".");
    const auto release = update::ParseManifest(body, update::Platform());
    if (!release) throw std::runtime_error("The release information could not be read.");
    page = release->page.empty() ? release->url : release->page;
    if (update::CompareVersions(release->version, installed) <= 0) throw std::runtime_error("Version " + installed + " is already the newest.");
    if (release->package_url.empty()) throw std::runtime_error("The release has no package for this system.");

    const fs::path download = install.parent_path() / (".pt-update-dl-" + platform.unique_id);
    auto remove_download = [&] {
        std::error_code error;
        if (update_journal::OwnFolder(install, download, ".pt-update-dl-")) fs::remove_all(download, error);
    };
    std::error_code error;
    GameUpdateResult result;
    fs::path downloaded;
    auto download_whole = [&] {
        if (!downloaded.empty()) return;
        fs::create_directories(download, error);
        if (error) throw std::runtime_error("Could not create the download folder next to the install: " + error.message() + ".");
        const fs::path file = download / Utf8Path(std::string(update::PackageAsset(update::Platform())));
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Could not write the download.");
        http::FetchRequest get;
        get.url = release->package_url;
        get.timeout_ms = 30000;
        get.max_bytes = release->package_size ? release->package_size : 2ull * 1024 * 1024 * 1024;
        get.allow = [](const std::string& url) { return update::AllowedAssetUrl(url); };
        get.write = [&](const char* data, size_t size) { out.write(data, std::streamsize(size)); return bool(out); };
        get.cancelled = [] { return hooks.cancelled && hooks.cancelled(); };
        progress.Begin(get.max_bytes);
        get.progress = [&](uint64_t done, uint64_t) { progress.At(done); };
        const auto fetched = http::Fetch(get);
        ++result.requests;
        result.downloaded += fetched.bytes;
        out.close();
        if (!fetched.ok) throw std::runtime_error("The download failed: " + fetched.error + ".");
        if (!out) throw std::runtime_error("Could not write the download (is the disk full?).");
        downloaded = file;
    };

    std::string why;
    bool range_ignored = false;
    std::optional<update::UpdateManifest> manifest;
    try {
        manifest = update::FetchSignedManifest(*release, &why, &range_ignored);
        if (!manifest && range_ignored) {
            phase(UpdatePhase::Downloading);
            download_whole();
            manifest = update::ReadSignedManifest(*release, downloaded, &why);
        }
        if (!manifest) throw std::runtime_error("The update could not be verified: " + why + ".");
    } catch (...) {
        remove_download();
        throw;
    }
    const update::PackageEntry package = manifest->packages.at(std::string(update::Platform()));
    const auto listed = manifest->files.find(std::string(update::Platform()));
    if (listed == manifest->files.end() || listed->second.empty()) {
        remove_download();
        throw std::runtime_error("The signed manifest lists no files for this system.");
    }

    std::vector<PayloadEntry> entries;
    for (const auto& file : listed->second) entries.push_back({file.path, file.size, file.sha256});
    const std::vector<PayloadEntry> needed = DifferingFiles(install, entries, platform.hash_file);
    result.needed = needed.size();
    std::set<std::string> needed_paths;
    uint64_t needed_packed = 0;
    bool have_ranges = true;
    for (const auto& entry : needed) needed_paths.insert(entry.path);
    std::vector<update::FileEntry> wanted;
    for (const auto& file : listed->second) {
        if (!file.packed) have_ranges = false;
        if (needed_paths.count(file.path)) {
            wanted.push_back(file);
            needed_packed += file.packed;
        }
    }
    const bool test = update::TestOverrides();
    bool ranged = downloaded.empty() && have_ranges && double(needed_packed) <= update::kRangeShare * double(package.size) &&
                  !(test && std::getenv("PT_UPDATE_TEST_FULL"));
    result.ranged = ranged;

    phase(UpdatePhase::Downloading);
    const unsigned char* payload = nullptr;
    size_t payload_size = 0;
    auto open_package = [&] {
        if (payload) return;
        download_whole();
        const fs::path& file = downloaded;
        if (pt::trailer::BodySize(file) != package.size) throw std::runtime_error("The download has the wrong size. Nothing was changed.");
        const auto opened = platform.open_payload(file);
        if (hash_package(file) != package.sha256)
            throw std::runtime_error("The download does not match the signed manifest (SHA-256). Nothing was changed.");
        auto listing = ListPayload(opened.first, opened.second);
        auto expected = listed->second;
        std::sort(listing.begin(), listing.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        std::sort(expected.begin(), expected.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        bool same = listing.size() == expected.size();
        for (size_t i = 0; same && i < listing.size(); ++i)
            same = listing[i].path == expected[i].path && listing[i].size == expected[i].size && listing[i].sha256 == expected[i].sha256;
        if (!same) throw std::runtime_error("The package's files do not match the signed manifest. Nothing was changed.");
        payload = opened.first;
        payload_size = opened.second;
    };
    try {
        InstallSteps steps;
        steps.version = StripV(manifest->version);
        steps.unique_id = platform.unique_id;
        steps.check_parents = platform.check_parents ? platform.check_parents : [](const fs::path&) {};
        steps.verify_integrity = [] {};
        steps.entries = [&entries] { return entries; };
        steps.needed = needed;
        steps.hash_file = platform.hash_file;
        steps.fetch = [&](const fs::path& folder, const std::vector<PayloadEntry>& want) {
            std::set<std::string> names;
            for (const auto& entry : want) names.insert(entry.path);
            if (ranged) {
                std::vector<update::FileEntry> files;
                for (const auto& file : listed->second)
                    if (names.count(file.path)) files.push_back(file);
                const RangeFetch fetched = FetchRanges(release->package_url, files, folder, platform.hash_file);
                result.requests += fetched.requests;
                result.downloaded += fetched.bytes;
                if (!fetched.range_ignored) {
                    if (platform.after_unpack) platform.after_unpack(folder);
                    std::vector<InstalledFile> written;
                    for (const auto& file : files) written.push_back({PathUtf8(Utf8Path(file.path)), file.sha256});
                    return written;
                }
                ranged = false;
                result.ranged = false;
                std::error_code e;
                for (const auto& file : files) fs::remove(Contained(folder, file.path), e);
            }
            open_package();
            auto written = UnpackPayload(payload, payload_size, folder, platform.hash_file, [&names](const std::string& name) { return names.count(name) > 0; });
            if (platform.after_unpack) platform.after_unpack(folder);
            return written;
        };
        steps.before_swap = [&] { phase(UpdatePhase::Installing); };
        steps.validate_runtime = platform.validate_runtime;
        steps.extract = [](const fs::path&, const fs::path&) { throw std::runtime_error("The game archives are missing; run the setup with your PKG or dump."); };
        steps.shortcut = [](const fs::path&) {};
        steps.keep_backup = true;
        steps.download = download;
        steps.pid = platform.pid;
        steps.fail_at = platform.fail_at;
        steps.process_alive = platform.process_alive;
        steps.known_folders = platform.known_folders;
        if (test) {
            if (const char* at = std::getenv("PT_UPDATE_TEST_PAUSE_AT")) steps.pause_at = std::atoi(at);
            if (const char* file = std::getenv("PT_UPDATE_TEST_CORRUPT")) steps.corrupt_after_swap = file;
        }
        result.version = steps.version;
        result.outcome = RunInstall({}, install, false, steps);
        if (platform.close_payload) platform.close_payload();
        LogUpdate(install, "in-game update to " + result.version + ": " + std::to_string(result.needed) + " of " + std::to_string(entries.size()) +
                               " files differed; downloaded " + std::to_string(result.downloaded) + " bytes in " + std::to_string(result.requests) +
                               (result.requests == 1 ? " request" : " requests") + (result.ranged ? " (parts of the package)" : " (the whole package)") +
                               " of a " + std::to_string(package.size) + " byte package");
        return result;
    } catch (...) {
        if (platform.close_payload) platform.close_payload();
        std::error_code e;
        if (!fs::exists(JournalFile(install), e)) remove_download();
        throw;
    }
}

enum class StartResult { Started, Failed, Unconfirmed };
inline StartResult WaitForStart(const fs::path& journal_file, const std::function<std::optional<int>()>& exit_code, std::chrono::seconds timeout,
                                int* code = nullptr) {
    const auto until = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto journal = update_journal::Read(journal_file);
        if (journal && journal->state == update_journal::State::Started) return StartResult::Started;
        if (exit_code) {
            if (const auto exited = exit_code()) {
                if (code) *code = *exited;
                const auto last = update_journal::Read(journal_file);
                if (last && last->state == update_journal::State::Started) return StartResult::Started;
                return *exited == 0 ? StartResult::Started : StartResult::Failed;
            }
        }
        if (std::chrono::steady_clock::now() >= until) return exit_code ? StartResult::Started : StartResult::Unconfirmed;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

inline bool FinishGameUpdate(const fs::path& journal_file) {
    const auto journal = update_journal::Read(journal_file);
    std::error_code error;
    if (journal) {
        update_journal::RemoveFolders(*journal);
        if (fs::exists(journal->backup, error) || fs::exists(journal->staging, error) || (!journal->download.empty() && fs::exists(journal->download, error))) {
            update_journal::SetState(journal_file, update_journal::State::Started);
            return false;
        }
    }
    fs::remove(journal_file, error);
    return true;
}

inline bool RollBackGameUpdate(const fs::path& journal_file, std::string* why) {
    const auto journal = update_journal::Read(journal_file);
    if (!journal) {
        if (why) *why = "the update journal is missing";
        return false;
    }
    if (!update_journal::Rollback(*journal, why)) return false;
    update_journal::RemoveFolders(*journal);
    std::error_code error;
    fs::remove(journal_file, error);
    return true;
}

}
