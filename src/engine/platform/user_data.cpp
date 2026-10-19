#include "engine/platform/user_data.h"
#include "engine/platform/os.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

namespace pt::platform {
namespace {

namespace fs = std::filesystem;

constexpr const char* kMarker = ".pt-user-data-migration-v1";
constexpr const char* kDeclinedHere = ".pt-user-data-declined";
constexpr const char* kSaveSlotPrefix = "PT_Save_Data";
std::atomic<unsigned long long> g_temp_counter{0};

std::string PathText(const fs::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void AddError(UserDataReport& report, const std::string& action,
              const fs::path& path, const std::error_code& ec) {
    report.errors.push_back(action + " '" + PathText(path) + "': " + ec.message());
}

void AddLegacyError(UserDataReport& report, const std::string& action, const fs::path& path, const std::error_code& ec) {
    report.legacy_errors.push_back(action + " '" + PathText(path) + "': " + ec.message());
}

class Sha256 {
public:
    using Digest = std::array<unsigned char, 32>;
    void Update(const unsigned char* data, size_t size) {
        total_ += size;
        while (size) {
            const size_t take = std::min(size, sizeof(block_) - used_);
            std::memcpy(block_ + used_, data, take);
            used_ += take;
            data += take;
            size -= take;
            if (used_ == sizeof(block_)) {
                Block(block_);
                used_ = 0;
            }
        }
    }
    Digest Finish() {
        const uint64_t bits = total_ * 8;
        const unsigned char pad = 0x80;
        Update(&pad, 1);
        const unsigned char zero = 0;
        while (used_ != 56) Update(&zero, 1);
        unsigned char length[8];
        for (int i = 0; i < 8; ++i) length[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        Update(length, 8);
        Digest out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<unsigned char>(h_[i] >> 24);
            out[i * 4 + 1] = static_cast<unsigned char>(h_[i] >> 16);
            out[i * 4 + 2] = static_cast<unsigned char>(h_[i] >> 8);
            out[i * 4 + 3] = static_cast<unsigned char>(h_[i]);
        }
        return out;
    }

private:
    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void Block(const unsigned char* p) {
        static constexpr uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }
    uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char block_[64]{};
    size_t used_ = 0;
    uint64_t total_ = 0;
};

struct FileFacts {
    uint64_t size = 0;
    Sha256::Digest hash{};
    fs::file_time_type mtime{};
    bool Same(const FileFacts& other) const { return size == other.size && hash == other.hash; }
};

bool ReadFacts(const fs::path& path, FileFacts& facts, std::error_code& ec) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }
    Sha256 sha;
    uint64_t size = 0;
    std::vector<char> buffer(256 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0) {
            sha.Update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<size_t>(count));
            size += static_cast<uint64_t>(count);
        }
    }
    if (in.bad()) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }
    facts.size = size;
    facts.hash = sha.Finish();
    facts.mtime = fs::last_write_time(path, ec);
    return !ec;
}

bool ReserveExclusive(const fs::path& path, std::error_code& ec) {
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    CloseHandle(file);
    ec.clear();
    return true;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }
    ::close(fd);
    ec.clear();
    return true;
#endif
}

bool IsReparsePoint(const fs::path& path) { return pt::os::IsLink(path); }

bool AtomicPublishNoReplace(const fs::path& temp,
                            const fs::path& target,
                            std::error_code& ec) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        ec.clear();
        return true;
    }
    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#elif defined(__linux__)
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif
    if (::syscall(SYS_renameat2, AT_FDCWD, temp.c_str(), AT_FDCWD, target.c_str(), RENAME_NOREPLACE) == 0) {
        ec.clear();
        return true;
    }
    if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP) {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }
    if (::link(temp.c_str(), target.c_str()) == 0) {
        if (::unlink(temp.c_str()) != 0) {
            ec = std::error_code(errno, std::generic_category());
            return false;
        }
        ec.clear();
        return true;
    }
    ec = std::error_code(errno, std::generic_category());
    return false;
#else
    if (::link(temp.c_str(), target.c_str()) == 0) {
        if (::unlink(temp.c_str()) != 0) {
            ec = std::error_code(errno, std::generic_category());
            return false;
        }
        ec.clear();
        return true;
    }
    ec = std::error_code(errno, std::generic_category());
    return false;
#endif
}

bool IsValidMarker(const fs::path& marker, bool& exists,
                   UserDataReport& report) {
    exists = false;
    std::error_code ec;
    const auto status = fs::symlink_status(marker, ec);
    if (ec == std::errc::no_such_file_or_directory) return true;
    if (ec) {
        AddError(report, "Cannot inspect migration marker", marker, ec);
        return false;
    }
    if (status.type() == fs::file_type::not_found) return true;
    exists = true;
    if (fs::is_symlink(status) || IsReparsePoint(marker) || !fs::is_regular_file(status)) {
        report.errors.push_back("Migration marker must be a regular file: '" + PathText(marker) + "'");
        return false;
    }
    std::ifstream input(marker, std::ios::binary);
    if (!input) {
        report.errors.push_back("Cannot read migration marker: '" + PathText(marker) + "'");
        return false;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad() || contents != "1\n") {
        report.errors.push_back("Migration marker is invalid: '" + PathText(marker) + "'");
        return false;
    }
    return true;
}

bool IsPlainFile(const fs::path& path) {
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    return !ec && fs::is_regular_file(status) && !IsReparsePoint(path);
}

bool MakeTempFile(const fs::path& parent, fs::path& temp,
                  std::error_code& ec) {
    for (int attempt = 0; attempt != 64; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto serial = g_temp_counter.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
        const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
        const auto process = static_cast<unsigned long long>(::getpid());
#endif
        temp = parent / (".pt-migrate-" + std::to_string(process) + "-" +
                         std::to_string(stamp) + "-" + std::to_string(serial));
        if (ReserveExclusive(temp, ec)) return true;
        if (ec != std::errc::file_exists) return false;
    }
    ec = std::make_error_code(std::errc::file_exists);
    return false;
}

bool WriteNewTextFile(const fs::path& target, const std::string& contents, std::error_code& ec) {
    fs::path temp;
    if (!MakeTempFile(target.parent_path(), temp, ec)) return false;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << contents;
        out.flush();
        const bool written = static_cast<bool>(out);
        out.close();
        if (!written || !out) {
            ec = std::make_error_code(std::errc::io_error);
            std::error_code ignored;
            fs::remove(temp, ignored);
            return false;
        }
    }
    if (AtomicPublishNoReplace(temp, target, ec)) return true;
    std::error_code cleanup_error;
    fs::remove(temp, cleanup_error);
    return false;
}

bool WriteMarker(const fs::path& destination, std::error_code& ec) {
    return WriteNewTextFile(destination / kMarker, "1\n", ec);
}

bool ProbeWritable(const fs::path& destination, std::error_code& ec) {
    fs::path probe;
    if (!MakeTempFile(destination, probe, ec)) return false;
    std::error_code remove_error;
    fs::remove(probe, remove_error);
    if (remove_error) {
        ec = remove_error;
        return false;
    }
    ec.clear();
    return true;
}

bool EnsureDestinationDirectories(const fs::path& root,
                                  const fs::path& relative,
                                  std::vector<std::string>& errors) {
    auto current = root;
    for (const auto& component : relative) {
        current /= component;
        std::error_code ec;
        auto status = fs::symlink_status(current, ec);
        if (ec == std::errc::no_such_file_or_directory) ec.clear();
        if (ec) {
            errors.push_back("Cannot inspect destination directory '" + PathText(current) + "': " + ec.message());
            return false;
        }
        if (!fs::exists(status)) {
            if (!fs::create_directory(current, ec) && ec != std::errc::file_exists) {
                errors.push_back("Cannot create user data directory '" + PathText(current) + "': " + ec.message());
                return false;
            }
            status = fs::symlink_status(current, ec);
            if (ec) {
                errors.push_back("Cannot inspect destination directory '" + PathText(current) + "': " + ec.message());
                return false;
            }
        }
        if (fs::is_symlink(status) || IsReparsePoint(current) || !fs::is_directory(status)) {
            errors.push_back("Destination path is not a real directory: '" + PathText(current) + "'");
            return false;
        }
    }
    return true;
}

std::string UtcNow() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M UTC", &parts);
    return text;
}

bool IsSaveSlot(const fs::path& relative) {
    if (!relative.parent_path().empty()) return false;
    const std::string name = PathText(relative.filename());
    const std::string prefix = kSaveSlotPrefix;
    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) return false;
    return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
}

bool IsCacheDir(const fs::path& relative) {
    if (!relative.parent_path().empty()) return false;
    const std::string name = PathText(relative.filename());
    const auto family = [&](const std::string& base) {
        return name == base || (name.size() > base.size() && name.compare(0, base.size(), base) == 0 && name[base.size()] == '-');
    };
    return family("enhanced-textures") || name == "pipeline-cache" || family("museum-previews") || family("loop-previews");
}

bool IsKeyFile(const fs::path& relative) {
    return IsSaveSlot(relative) || relative == fs::path("pt.ini") || relative == fs::path("PT_Save_Data.sav");
}

struct LegacyFile {
    fs::path relative;
    fs::path path;
    FileFacts facts;
};

struct LegacyScan {
    std::vector<LegacyFile> files;
    std::vector<fs::path> directories;
    std::vector<fs::path> caches;
    size_t left_behind = 0;
    bool linked_entries = false;
};

bool ScanLegacy(const fs::path& legacy, LegacyScan& scan, bool with_facts, UserDataReport& report) {
    std::error_code ec;
    fs::recursive_directory_iterator it(legacy, ec), end;
    if (ec) {
        AddLegacyError(report, "Cannot read the old data directory", legacy, ec);
        return false;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const auto from = it->path();
        const auto relative = from.lexically_relative(legacy);
        if (relative.empty() || relative == ".") continue;
        const auto status = it->symlink_status(ec);
        if (ec) {
            AddLegacyError(report, "Cannot inspect old entry", from, ec);
            return false;
        }
        if (fs::is_symlink(status) || IsReparsePoint(from)) {
            report.warnings.push_back("Skipped linked old path (left in place): '" + PathText(from) + "'");
            scan.linked_entries = true;
            it.disable_recursion_pending();
            continue;
        }
        if (fs::is_directory(status)) {
            if (IsCacheDir(relative)) {
                scan.caches.push_back(from);
                it.disable_recursion_pending();
                continue;
            }
            scan.directories.push_back(from);
            continue;
        }
        if (!fs::is_regular_file(status)) {
            report.warnings.push_back("Skipped special old entry (left in place): '" + PathText(from) + "'");
            scan.linked_entries = true;
            continue;
        }
        LegacyFile file{relative, from, {}};
        if (with_facts && !ReadFacts(from, file.facts, ec)) {
            if (IsKeyFile(relative)) {
                AddLegacyError(report, "Cannot read old file", from, ec);
                return false;
            }
            report.warnings.push_back("Cannot read the old file '" + PathText(from) + "' (in use?); it stays in the old folder for now");
            ++scan.left_behind;
            ec.clear();
            continue;
        }
        scan.files.push_back(std::move(file));
    }
    if (ec) {
        AddLegacyError(report, "Cannot scan the old data directory", legacy, ec);
        return false;
    }
    return true;
}

enum class StepKind {
    Identical,
    Copy,
    CopyBak,
    Replace,
};

struct Step {
    StepKind kind = StepKind::Copy;
    LegacyFile file;
    fs::path target;
    fs::path staged;
    bool published = false;
    bool skipped = false;
};

struct Aside {
    fs::path from, to;
    bool done = false;
};

struct Plan {
    std::vector<Step> steps;
    std::vector<Aside> asides;
    std::set<fs::path> reserved;
    bool newer_in_old_folder = false;
};

bool PickBak(const fs::path& path, const FileFacts* content, Plan& plan, fs::path& out, bool& identical) {
    identical = false;
    for (int n = 1; n < 100; ++n) {
        fs::path candidate = path;
        candidate += n == 1 ? std::string(".bak") : ".bak" + std::to_string(n);
        if (plan.reserved.count(candidate)) continue;
        std::error_code ec;
        const auto status = fs::symlink_status(candidate, ec);
        if (ec == std::errc::no_such_file_or_directory || (!ec && status.type() == fs::file_type::not_found)) {
            plan.reserved.insert(candidate);
            out = candidate;
            return true;
        }
        if (ec) return false;
        if (content && IsPlainFile(candidate)) {
            FileFacts existing;
            if (ReadFacts(candidate, existing, ec) && existing.Same(*content)) {
                identical = true;
                out = candidate;
                return true;
            }
        }
    }
    return false;
}

std::string Rel(const fs::path& relative) { return PathText(relative.generic_u8string()); }

bool AddBakStep(const LegacyFile& file, const fs::path& target, Plan& plan, UserDataReport& report) {
    fs::path bak;
    bool identical = false;
    if (!PickBak(target, &file.facts, plan, bak, identical)) {
        report.legacy_errors.push_back("No free .bak name beside '" + PathText(target) + "'");
        return false;
    }
    plan.steps.push_back({identical ? StepKind::Identical : StepKind::CopyBak, file, bak});
    report.notes.push_back(Rel(file.relative) + ": the destination's copy is newer and stays; the old one " +
                           (identical ? "is already there as " : "is kept as ") + PathText(bak.filename()));
    return true;
}

bool AddAside(const fs::path& target, const fs::path& relative, Plan& plan, UserDataReport& report) {
    fs::path bak;
    bool identical = false;
    if (!PickBak(target, nullptr, plan, bak, identical)) {
        report.legacy_errors.push_back("No free .bak name beside '" + PathText(target) + "'");
        return false;
    }
    plan.asides.push_back({target, bak});
    report.notes.push_back(Rel(relative) + ": the old folder's copy is newer; the destination's one is kept as " + PathText(bak.filename()));
    return true;
}

bool BuildPlan(const fs::path& destination, const LegacyScan& scan, Plan& plan, UserDataReport& report) {
    std::vector<const LegacyFile*> saves;
    for (const auto& file : scan.files) {
        if (IsSaveSlot(file.relative)) saves.push_back(&file);
    }
    struct DestSlot {
        fs::path path;
        FileFacts facts;
    };
    std::vector<DestSlot> destSaves;
    if (!saves.empty()) {
        std::error_code ec;
        for (fs::directory_iterator it(destination, ec), end; !ec && it != end; it.increment(ec)) {
            const auto name = it->path().filename();
            if (!IsSaveSlot(name) || !IsPlainFile(it->path())) continue;
            DestSlot slot{it->path(), {}};
            std::error_code read_error;
            if (!ReadFacts(slot.path, slot.facts, read_error)) {
                AddLegacyError(report, "Cannot read destination save", slot.path, read_error);
                return false;
            }
            destSaves.push_back(std::move(slot));
        }
        if (ec) {
            AddLegacyError(report, "Cannot list the destination", destination, ec);
            return false;
        }
        const auto destFor = [&](const LegacyFile& file) -> const DestSlot* {
            for (const auto& slot : destSaves) {
                if (slot.path.filename() == file.relative.filename()) return &slot;
            }
            return nullptr;
        };
        const bool allIdentical = std::all_of(saves.begin(), saves.end(), [&](const LegacyFile* file) {
            const DestSlot* slot = destFor(*file);
            return slot && slot->facts.Same(file->facts);
        });
        fs::file_time_type newestOld = fs::file_time_type::min(), newestDest = fs::file_time_type::min();
        for (const auto* file : saves) newestOld = std::max(newestOld, file->facts.mtime);
        for (const auto& slot : destSaves) newestDest = std::max(newestDest, slot.facts.mtime);
        const bool oldWins = !allIdentical && (destSaves.empty() || newestOld > newestDest);
        if (allIdentical) {
            report.notes.push_back("saves: the destination has the same save files");
        } else if (oldWins) {
            report.notes.push_back(destSaves.empty() ? "saves: the destination has no saves; the old ones are used"
                                                     : "saves: the old folder's saves are newer; they replace the destination's");
            plan.newer_in_old_folder = true;
        } else {
            report.notes.push_back("saves: the destination's saves are newer and stay");
        }
        for (const auto* file : saves) {
            const DestSlot* slot = destFor(*file);
            const fs::path target = destination / file->relative;
            if (slot && slot->facts.Same(file->facts)) {
                plan.steps.push_back({StepKind::Identical, *file, target});
            } else if (oldWins) {
                if (slot && !AddAside(target, file->relative, plan, report)) return false;
                plan.steps.push_back({slot ? StepKind::Replace : StepKind::Copy, *file, target});
                plan.reserved.insert(target);
            } else if (!AddBakStep(*file, target, plan, report)) {
                return false;
            }
        }
        if (oldWins) {
            for (const auto& slot : destSaves) {
                const bool covered = std::any_of(saves.begin(), saves.end(), [&](const LegacyFile* file) {
                    return file->relative.filename() == slot.path.filename();
                });
                if (!covered && !AddAside(slot.path, slot.path.filename(), plan, report)) return false;
            }
        }
    }

    for (const auto& file : scan.files) {
        if (IsSaveSlot(file.relative)) continue;
        const fs::path target = destination / file.relative;
        std::error_code ec;
        const auto status = fs::symlink_status(target, ec);
        if (ec == std::errc::no_such_file_or_directory || (!ec && status.type() == fs::file_type::not_found)) {
            plan.steps.push_back({StepKind::Copy, file, target});
            plan.reserved.insert(target);
            continue;
        }
        if (ec) {
            AddLegacyError(report, "Cannot inspect destination file", target, ec);
            return false;
        }
        if (!fs::is_regular_file(status) || fs::is_symlink(status) || IsReparsePoint(target)) {
            report.legacy_errors.push_back("Destination entry blocks the old file: '" + PathText(target) + "'");
            return false;
        }
        FileFacts destFacts;
        if (!ReadFacts(target, destFacts, ec)) {
            AddLegacyError(report, "Cannot read destination file", target, ec);
            return false;
        }
        if (destFacts.Same(file.facts)) {
            plan.steps.push_back({StepKind::Identical, file, target});
        } else if (file.facts.mtime > destFacts.mtime) {
            if (!AddAside(target, file.relative, plan, report)) return false;
            plan.steps.push_back({StepKind::Replace, file, target});
            plan.reserved.insert(target);
            if (file.relative == fs::path("pt.ini")) plan.newer_in_old_folder = true;
        } else if (!AddBakStep(file, target, plan, report)) {
            return false;
        }
    }
    return true;
}

bool StageCopy(Step& step, const UserDataHooks& hooks, std::string& error) {
    std::error_code ec;
    fs::path temp;
    if (!MakeTempFile(step.target.parent_path(), temp, ec)) {
        error = "Cannot create a temporary file in '" + PathText(step.target.parent_path()) + "': " + ec.message();
        return false;
    }
    step.staged = temp;
    {
        std::ifstream in(step.file.path, std::ios::binary);
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!in || !out) {
            error = "Cannot open '" + PathText(in ? temp : step.file.path) + "'";
            return false;
        }
        Sha256 sha;
        uint64_t size = 0;
        std::vector<char> buffer(256 * 1024);
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = in.gcount();
            if (count > 0) {
                sha.Update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<size_t>(count));
                size += static_cast<uint64_t>(count);
                out.write(buffer.data(), count);
            }
        }
        out.flush();
        const bool written = !in.bad() && static_cast<bool>(out);
        out.close();
        if (!written || !out) {
            error = "Cannot copy '" + PathText(step.file.path) + "' to '" + PathText(temp) + "'";
            return false;
        }
        FileFacts read{size, sha.Finish(), {}};
        if (!read.Same(step.file.facts)) {
            error = "The old file changed during the move: '" + PathText(step.file.path) + "'";
            return false;
        }
    }
    fs::last_write_time(temp, step.file.facts.mtime, ec);
    if (ec) {
        error = "Cannot set the time of '" + PathText(temp) + "': " + ec.message();
        return false;
    }
    if (hooks.after_stage) hooks.after_stage(temp);
    FileFacts copy;
    if (!ReadFacts(temp, copy, ec) || !copy.Same(step.file.facts)) {
        error = "The copy of '" + PathText(step.file.path) + "' does not match it (size " + std::to_string(copy.size) + " of " +
                std::to_string(step.file.facts.size) + ", or SHA-256 differs)";
        return false;
    }
    return true;
}

void DropStaged(Plan& plan) {
    for (auto& step : plan.steps) {
        if (!step.staged.empty() && !step.published) {
            std::error_code ignored;
            fs::remove(step.staged, ignored);
            step.staged.clear();
        }
    }
}

void Undo(Plan& plan, UserDataReport& report) {
    for (auto it = plan.steps.rbegin(); it != plan.steps.rend(); ++it) {
        if (!it->published) continue;
        std::error_code ec;
        fs::remove(it->target, ec);
        if (ec) AddLegacyError(report, "Cannot take back the copy", it->target, ec);
        it->published = false;
    }
    for (auto it = plan.asides.rbegin(); it != plan.asides.rend(); ++it) {
        if (!it->done) continue;
        std::error_code ec;
        if (!AtomicPublishNoReplace(it->to, it->from, ec)) AddLegacyError(report, "Cannot restore", it->from, ec);
        it->done = false;
    }
    DropStaged(plan);
}

bool WriteLegacyMarker(const fs::path& legacy, const char* name, const std::string& text, UserDataReport& report) {
    const fs::path parent = legacy.parent_path();
    std::error_code ec;
    if (WriteNewTextFile(parent / name, text, ec)) return true;
    if (IsPlainFile(parent / name)) return true;
    report.warnings.push_back("Cannot write '" + PathText(parent / name) + "': " + ec.message());
    return false;
}

void RemoveEmptyFolders(std::vector<fs::path> directories) {
    std::sort(directories.begin(), directories.end(), [](const fs::path& a, const fs::path& b) {
        return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
    });
    for (const auto& directory : directories) {
        std::error_code ec;
        fs::remove(directory, ec);
    }
}

void RemoveCaches(const std::vector<fs::path>& caches, UserDataReport& report) {
    if (caches.empty()) return;
    size_t removed = 0, kept = 0;
    for (const auto& cache : caches) {
        std::vector<fs::path> files, directories{cache};
        std::error_code ec;
        for (fs::recursive_directory_iterator it(cache, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code status_error;
            const auto status = it->symlink_status(status_error);
            if (status_error || fs::is_symlink(status) || IsReparsePoint(it->path())) {
                it.disable_recursion_pending();
                ++kept;
            } else if (fs::is_directory(status)) {
                directories.push_back(it->path());
            } else {
                files.push_back(it->path());
            }
        }
        for (const auto& file : files) {
            std::error_code remove_error;
            if (fs::remove(file, remove_error)) ++removed;
            else ++kept;
        }
        RemoveEmptyFolders(std::move(directories));
    }
    std::string names;
    for (const auto& cache : caches) names += (names.empty() ? "" : ", ") + PathText(cache.filename());
    report.notes.push_back("old caches (" + names + ") are not moved, the game rebuilds them: " + std::to_string(removed) +
                           " files deleted" + (kept ? ", " + std::to_string(kept) + " in use and left in place" : std::string()));
}

bool ExecutePlan(const fs::path& destination, const fs::path& legacy, const LegacyScan& scan, Plan& plan,
                 const UserDataHooks& hooks, UserDataReport& report) {
    bool kept = scan.left_behind != 0;
    bool key_failed = false;
    for (auto& step : plan.steps) {
        if (step.kind == StepKind::Identical) continue;
        if (!EnsureDestinationDirectories(destination, step.target.parent_path().lexically_relative(destination), report.legacy_errors)) {
            DropStaged(plan);
            return false;
        }
        std::string error;
        if (!StageCopy(step, hooks, error)) {
            if (IsKeyFile(step.file.relative)) {
                report.legacy_errors.push_back(error);
                DropStaged(plan);
                return false;
            }
            report.warnings.push_back(error + "; it stays in the old folder for now");
            std::error_code ignored;
            if (!step.staged.empty()) fs::remove(step.staged, ignored);
            step.staged.clear();
            step.skipped = true;
            kept = true;
        }
    }
    for (auto& aside : plan.asides) {
        if (std::any_of(plan.steps.begin(), plan.steps.end(), [&](const Step& step) { return step.skipped && step.target == aside.from; })) {
            continue;
        }
        std::error_code ec;
        if (!AtomicPublishNoReplace(aside.from, aside.to, ec)) {
            AddLegacyError(report, "Cannot keep the destination's file as .bak", aside.from, ec);
            Undo(plan, report);
            return false;
        }
        aside.done = true;
    }
    for (auto& step : plan.steps) {
        if (step.kind == StepKind::Identical || step.skipped) continue;
        std::error_code ec;
        if (!AtomicPublishNoReplace(step.staged, step.target, ec)) {
            AddLegacyError(report, "Cannot publish", step.target, ec);
            Undo(plan, report);
            return false;
        }
        step.published = true;
        FileFacts published;
        if (!ReadFacts(step.target, published, ec) || !published.Same(step.file.facts)) {
            report.legacy_errors.push_back("The published copy does not match its original: '" + PathText(step.target) + "'");
            Undo(plan, report);
            return false;
        }
        if (step.kind == StepKind::CopyBak) ++report.files_backed_up;
        else ++report.files_copied;
    }
    report.files_backed_up += static_cast<size_t>(std::count_if(plan.asides.begin(), plan.asides.end(), [](const Aside& a) { return a.done; }));

    for (const auto& step : plan.steps) {
        if (step.skipped) continue;
        const auto problem = [&](const std::string& text) {
            if (IsKeyFile(step.file.relative)) {
                report.legacy_errors.push_back(text);
                key_failed = true;
            } else {
                report.warnings.push_back(text);
                kept = true;
            }
        };
        std::error_code ec;
        FileFacts now, twin;
        if (!ReadFacts(step.file.path, now, ec) || !now.Same(step.file.facts)) {
            problem("The old file changed during the move and was kept: '" + PathText(step.file.path) + "'");
            continue;
        }
        if (!ReadFacts(step.target, twin, ec) || !twin.Same(step.file.facts)) {
            problem("No verified copy of '" + PathText(step.file.path) + "'; it was kept");
            continue;
        }
        fs::remove(step.file.path, ec);
        if (ec) {
            problem("Cannot delete the old file (its copy is verified) '" + PathText(step.file.path) + "': " + ec.message());
            continue;
        }
        ++report.files_removed;
    }
    if (!key_failed) RemoveCaches(scan.caches, report);
    RemoveEmptyFolders(scan.directories);
    std::error_code ec;
    fs::remove(legacy, ec);
    report.removed_legacy = !fs::exists(fs::symlink_status(legacy, ec));
    if (key_failed) {
        report.legacy_failed = true;
        return true;
    }
    if (kept) {
        report.notes.push_back("some old files other than saves and settings are in use and stay in the old folder; a later start "
                               "cleans them up");
        return true;
    }
    WriteLegacyMarker(legacy, kLegacyMovedMarker,
                      "Moved to: " + PathText(destination) + "\nDate: " + UtcNow() + "\n", report);
    return true;
}

void RemoveLeftoverCaches(const fs::path& legacy, UserDataReport& report) {
    UserDataReport quiet;
    LegacyScan scan;
    if (!ScanLegacy(legacy, scan, false, quiet) || !scan.files.empty() || scan.linked_entries || scan.caches.empty()) return;
    RemoveCaches(scan.caches, report);
    RemoveEmptyFolders(scan.directories);
    std::error_code ec;
    fs::remove(legacy, ec);
    if (!fs::exists(fs::symlink_status(legacy, ec))) report.notes.push_back("the old profile folder's leftover caches are gone");
}

enum class LockResult { Held, Busy, Unavailable };

std::string LockKey(const fs::path& legacy) {
    std::error_code ec;
    fs::path full = fs::absolute(legacy, ec);
    if (ec) full = legacy;
    std::string text = PathText(full.lexically_normal().generic_u8string());
    while (text.size() > 1 && text.back() == '/') text.pop_back();
#ifdef _WIN32
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    Sha256 sha;
    sha.Update(reinterpret_cast<const unsigned char*>(text.data()), text.size());
    const auto digest = sha.Finish();
    static const char* hex = "0123456789abcdef";
    std::string key;
    for (int i = 0; i < 8; ++i) {
        key += hex[digest[i] >> 4];
        key += hex[digest[i] & 15];
    }
    return key;
}

class LegacyLock {
public:
    LegacyLock() = default;
    LegacyLock(const LegacyLock&) = delete;
    LegacyLock& operator=(const LegacyLock&) = delete;
    ~LegacyLock() {
#ifdef _WIN32
        if (held_) ReleaseMutex(mutex_);
        if (mutex_) CloseHandle(mutex_);
#else
        if (fd_ >= 0) ::close(fd_);
#endif
    }

    LockResult Acquire(const fs::path& legacy, std::chrono::milliseconds wait, std::string& detail) {
        const std::string key = LockKey(legacy);
#ifdef _WIN32
        const std::string name = "Local\\pt-port-old-saves-" + key;
        const std::wstring wide(name.begin(), name.end());
        mutex_ = CreateMutexW(nullptr, FALSE, wide.c_str());
        if (!mutex_) {
            detail = std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
            return LockResult::Unavailable;
        }
        const DWORD result = WaitForSingleObject(mutex_, static_cast<DWORD>(std::max<long long>(0, wait.count())));
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
            held_ = true;
            return LockResult::Held;
        }
        if (result == WAIT_TIMEOUT) return LockResult::Busy;
        detail = std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
        return LockResult::Unavailable;
#else
        std::string dir = pt::os::GetEnv("XDG_RUNTIME_DIR");
        if (dir.empty()) dir = "/tmp";
        const fs::path file = fs::path(dir) / ("pt-port-old-saves-" + std::to_string(::getuid()) + "-" + key + ".lock");
        fd_ = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd_ < 0) {
            detail = PathText(file) + ": " + std::error_code(errno, std::generic_category()).message();
            return LockResult::Unavailable;
        }
        const auto deadline = std::chrono::steady_clock::now() + wait;
        for (;;) {
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) return LockResult::Held;
            if (errno != EWOULDBLOCK && errno != EINTR) {
                detail = PathText(file) + ": " + std::error_code(errno, std::generic_category()).message();
                return LockResult::Unavailable;
            }
            if (std::chrono::steady_clock::now() >= deadline) return LockResult::Busy;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#endif
    }

private:
#ifdef _WIN32
    HANDLE mutex_ = nullptr;
    bool held_ = false;
#else
    int fd_ = -1;
#endif
};

bool OldFolderMayNeedWork(const fs::path& legacy) {
    if (legacy.empty()) return false;
    std::error_code ec;
    const auto status = fs::symlink_status(legacy, ec);
    if (ec || status.type() == fs::file_type::not_found) return false;
    return !IsPlainFile(legacy.parent_path() / kLegacyDeclinedMarker);
}

}

UserDataReport PrepareUserDataDirectory(const fs::path& destination,
                                       const fs::path& legacy,
                                       bool migrate_legacy,
                                       const LegacyAsk& ask,
                                       const UserDataHooks& hooks) {
    UserDataReport report;
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        AddError(report, "Cannot create user data directory", destination, ec);
        return report;
    }
    const auto dest_status = fs::symlink_status(destination, ec);
    if (ec || fs::is_symlink(dest_status) || IsReparsePoint(destination) ||
        !fs::is_directory(dest_status)) {
        if (ec) AddError(report, "Cannot inspect user data directory", destination, ec);
        else report.errors.push_back("User data destination must be a real directory: '" + PathText(destination) + "'");
        return report;
    }
    if (!ProbeWritable(destination, ec)) {
        AddError(report, "User data directory is not writable", destination, ec);
        return report;
    }
    if (!migrate_legacy) {
        report.success = true;
        return report;
    }

    const auto marker = destination / kMarker;
    bool markerExists = false;
    if (!IsValidMarker(marker, markerExists, report)) return report;
    report.success = true;
    const auto finishMarker = [&] {
        if (markerExists) return;
        if (!WriteMarker(destination, ec)) {
            bool appeared = false;
            if (IsValidMarker(marker, appeared, report) && appeared) return;
            if (report.errors.empty()) AddError(report, "Cannot write user data migration marker", marker, ec);
            report.success = false;
        }
    };

    LegacyLock lock;
    if (OldFolderMayNeedWork(legacy)) {
        std::string detail;
        switch (lock.Acquire(legacy, hooks.lock_wait.value_or(kLegacyLockWait), detail)) {
        case LockResult::Busy:
            report.legacy_busy = true;
            report.notes.push_back("another copy of the game is handling the old profile folder; it is left alone this start");
            return report;
        case LockResult::Unavailable:
            report.warnings.push_back("Cannot take the old profile folder's lock (" + detail + "); going on without it");
            break;
        case LockResult::Held:
            break;
        }
        if (hooks.after_lock) hooks.after_lock();
        if (!IsValidMarker(marker, markerExists, report)) {
            report.success = false;
            return report;
        }
    }

    bool legacy_exists = false;
    if (!legacy.empty()) {
        const auto legacy_status = fs::symlink_status(legacy, ec);
        if (!ec && fs::is_directory(legacy_status) && !fs::is_symlink(legacy_status) && !IsReparsePoint(legacy)) {
            legacy_exists = true;
        } else if (!ec && (fs::is_symlink(legacy_status) || IsReparsePoint(legacy))) {
            report.warnings.push_back("Skipped linked legacy data directory: '" + PathText(legacy) + "'");
        } else if (ec == std::errc::no_such_file_or_directory) {
            ec.clear();
        } else if (ec) {
            report.warnings.push_back("Cannot inspect legacy data directory '" + PathText(legacy) + "': " + ec.message());
        } else if (fs::exists(legacy_status)) {
            report.warnings.push_back("Legacy data path is not a real directory: '" + PathText(legacy) + "'");
        }
    }
    if (legacy_exists) {
        std::error_code samePathError;
        if (fs::equivalent(legacy, destination, samePathError)) {
            report.warnings.push_back("Legacy and destination data paths are the same; skipped self-migration");
            legacy_exists = false;
        } else if (samePathError) {
            report.warnings.push_back("Cannot compare legacy and destination data paths: " + samePathError.message());
            legacy_exists = false;
        }
    }
    if (legacy_exists) {
        const fs::path parent = legacy.parent_path();
        const bool moved = IsPlainFile(parent / kLegacyMovedMarker), declined = IsPlainFile(parent / kLegacyDeclinedMarker);
        if (moved || declined) {
            report.notes.push_back("the old folder was already moved or kept by choice (" +
                                   PathText(parent / (moved ? kLegacyMovedMarker : kLegacyDeclinedMarker)) + ")");
            if (moved && !declined) RemoveLeftoverCaches(legacy, report);
            legacy_exists = false;
        }
    }
    if (!legacy_exists) {
        finishMarker();
        return report;
    }

    LegacyScan names;
    if (!ScanLegacy(legacy, names, false, report)) {
        report.legacy_failed = true;
        return report;
    }
    if (names.files.empty()) {
        finishMarker();
        return report;
    }

    const bool earlierCopy = markerExists && !IsPlainFile(destination / kDeclinedHere);
    if (markerExists && !earlierCopy) {
        report.notes.push_back("the player kept the old folder (No) in this installation");
        return report;
    }
    const auto decline = [&] {
        report.declined_legacy = true;
        WriteLegacyMarker(legacy, kLegacyDeclinedMarker,
                          "Kept here by the player's choice, not moved to: " + PathText(destination) + "\nDate: " + UtcNow() + "\n",
                          report);
        std::error_code ignored;
        WriteNewTextFile(destination / kDeclinedHere, "1\n", ignored);
        finishMarker();
    };

    if (!earlierCopy) {
        const std::optional<bool> answer = ask ? ask(LegacyQuestion::Move, legacy) : std::nullopt;
        if (!answer) {
            report.legacy_pending = true;
            return report;
        }
        report.asked = true;
        if (!*answer) {
            decline();
            return report;
        }
    }

    LegacyScan scan;
    Plan plan;
    if (!ScanLegacy(legacy, scan, true, report) || !BuildPlan(destination, scan, plan, report)) {
        report.legacy_failed = true;
        return report;
    }
    if (earlierCopy) {
        if (plan.newer_in_old_folder) {
            const std::optional<bool> answer = ask ? ask(LegacyQuestion::NewerCopies, legacy) : std::nullopt;
            if (!answer) {
                report.legacy_pending = true;
                return report;
            }
            report.asked = true;
            if (!*answer) {
                decline();
                return report;
            }
        } else {
            report.notes.push_back("an earlier version copied this folder; nothing in it is newer than the destination's saves");
        }
    }
    if (!ExecutePlan(destination, legacy, scan, plan, hooks, report)) {
        report.legacy_failed = true;
        return report;
    }
    report.migrated_legacy = !earlierCopy || report.asked;
    report.cleaned_legacy = earlierCopy && !report.asked;
    finishMarker();
    return report;
}

}
