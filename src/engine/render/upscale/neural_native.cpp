#include "engine/render/upscale/neural_native.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <system_error>
#include <thread>

#include "engine/core/ascii.h"
#include "engine/core/log.h"
#include "engine/core/resource_path.h"
#include "engine/platform/json_value.h"
#include "engine/platform/os.h"
#include "engine/render/upscale/neural_unlock.h"

#ifdef _WIN32
#include <windows.h>
#include <zstd.h>
#endif

namespace pt::nr_native {

namespace {

constexpr uint32_t kNvidia = 0x10DE;
constexpr uint32_t kBundleMagic = 0xBA55ED50;

std::optional<Install> g_active;
std::filesystem::path g_data_dir;
bool g_env_native = false;

std::string EnvOr(const char* name, std::string_view fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : std::string(fallback);
}

std::filesystem::path Root(const std::filesystem::path& data_dir) { return data_dir / std::string(kFolder); }
std::filesystem::path VersionDir(const std::filesystem::path& data_dir) { return Root(data_dir) / std::string(kRuntimeVersion); }
std::filesystem::path CodeDir(const std::filesystem::path& dir, int arch) { return dir / std::format("sm_{}", arch); }

uint64_t SizeOf(const std::filesystem::path& p) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(p, ec);
    return ec ? 0 : n;
}

template <typename T>
T Read(std::span<const uint8_t> bytes, size_t at) {
    T v{};
    if (at + sizeof(T) <= bytes.size()) std::memcpy(&v, bytes.data() + at, sizeof(T));
    return v;
}

std::vector<std::pair<std::string, uint64_t>> ReadBundleList(const std::filesystem::path& code) {
    std::vector<std::pair<std::string, uint64_t>> out;
    std::ifstream in(code / "bundles.txt");
    std::string name;
    uint64_t size = 0;
    while (in >> name >> size) out.push_back({name, size});
    return out;
}

bool CodeComplete(const std::filesystem::path& code) {
    const auto list = ReadBundleList(code);
    if (list.size() != kBundles) return false;
    for (const auto& [name, size] : list)
        if (SizeOf(code / name) != size || size == 0) return false;
    return true;
}

#ifdef _WIN32
class Mapped {
public:
    explicit Mapped(const std::filesystem::path& path) {
        file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file_, &size) || size.QuadPart == 0) return;
        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_) return;
        view_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0);
        if (view_) size_ = static_cast<size_t>(size.QuadPart);
    }
    ~Mapped() {
        if (view_) UnmapViewOfFile(view_);
        if (mapping_) CloseHandle(mapping_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    }
    Mapped(const Mapped&) = delete;
    Mapped& operator=(const Mapped&) = delete;
    std::span<const uint8_t> Bytes() const { return {static_cast<const uint8_t*>(view_), size_}; }
    bool Ok() const { return view_ != nullptr; }

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    void* view_ = nullptr;
    size_t size_ = 0;
};
#endif

}

int CodeArchFor(uint32_t vendor_id, uint32_t device_id, std::string_view adapter) {
    if (vendor_id != kNvidia) return -1;
    const std::string name = AsciiLowerCopy(std::string(adapter));
    const size_t rtx = name.find("rtx");
    if (rtx == std::string::npos) return -1;
    if (device_id != 0) {
        if (device_id >= 0x2900) return 0;
        if (device_id >= 0x2600) return 89;
        if (device_id >= 0x2200) return 86;
        if (device_id >= 0x1E00) return 75;
        return -1;
    }
    size_t i = rtx + 3;
    while (i < name.size() && !std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    if (i + 1 < name.size() && std::isdigit(static_cast<unsigned char>(name[i + 1]))) {
        const int series = (name[i] - '0') * 10 + (name[i + 1] - '0');
        if (series >= 50 && series < 100) return 0;
        if (series == 40) return 89;
        if (series == 30) return 86;
        if (series == 20) return 75;
    }
    return -1;
}

std::optional<Install> Read(const std::filesystem::path& data_dir, int arch) {
    Install in;
    in.dir = VersionDir(data_dir);
    in.runtime = in.dir / std::string(kRuntimeFile);
    in.version = std::string(kRuntimeVersion);
    std::ifstream record(in.dir / std::string(kRecord));
    if (!record) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(record)), {});
    metadata_json::Value v;
    if (!metadata_json::Reader(text).Parse(v) || !v.object) return std::nullopt;
    const auto sha = v.members.find("runtime_sha256");
    if (sha == v.members.end() || sha->second.text != kRuntimeSha256) return std::nullopt;
    if (SizeOf(in.runtime) != kRuntimeSize) return std::nullopt;
    if (arch > 0) {
        if (!CodeComplete(CodeDir(in.dir, arch))) return std::nullopt;
        in.code = CodeDir(in.dir, arch);
        in.arch = arch;
    }
    return in;
}

void Startup(const std::filesystem::path& data_dir) {
    g_env_native = std::getenv("PT_NR_NATIVE") != nullptr;
    std::error_code ec;
    const std::filesystem::path root = Root(data_dir);
    if (std::filesystem::exists(root / std::string(kRemoveMarker), ec)) {
        const auto removed = std::filesystem::remove_all(root, ec);
        if (ec) LogWarn("dlss5 native: the remove left for this start failed: {}", ec.message());
        else LogInfo("dlss5 native: removed at start as asked ({} files)", removed);
    }
    g_data_dir = data_dir;
    g_active = Read(data_dir, 0);
    if (g_active) LogInfo("dlss5 native: installed {} in {}", g_active->version, os::PathToUtf8(g_active->dir));
}

bool UseCard(int arch) {
    if (!g_active) return false;
    if (arch < 0) {
        LogInfo("dlss5 native: installed, but this card has no DLSS 5");
        g_active.reset();
        return false;
    }
    if (arch == 0) return true;
    const auto with_code = Read(g_data_dir, arch);
    if (!with_code) {
        LogInfo("dlss5 native: installed without this card's GPU code (sm_{}); Unlock adds it", arch);
        g_active.reset();
        return false;
    }
    g_active = with_code;
    LogInfo("dlss5 native: this card's GPU code: sm_{}", arch);
    return true;
}

const std::optional<Install>& Active() { return g_active; }
bool Enabled() { return g_env_native || g_active.has_value(); }

std::filesystem::path CompanionPath() {
    if (const char* env = std::getenv("PT_NR_NATIVE_COMPANION"); env && *env) return os::PathFromUtf8(env);
    return g_active ? ExecutableDir() / std::string(kCompanion) : std::filesystem::path();
}
std::filesystem::path CodePath() {
    if (const char* env = std::getenv("PT_NR_CUBIN_DIR"); env && *env) return os::PathFromUtf8(env);
    return g_active ? g_active->code : std::filesystem::path();
}

std::vector<Bundle> FindBundles(std::span<const uint8_t> dll) {
    std::vector<Bundle> out;
    for (size_t at = 0; at + 16 <= dll.size();) {
        if (Read<uint32_t>(dll, at) != kBundleMagic) {
            at += 4;
            continue;
        }
        const uint16_t version = Read<uint16_t>(dll, at + 4), header = Read<uint16_t>(dll, at + 6);
        const uint64_t size = Read<uint64_t>(dll, at + 8);
        if (version != 1 || header != 16 || size == 0 || size > 200'000'000 || at + header + size > dll.size()) {
            at += 4;
            continue;
        }
        Bundle b;
        b.offset = at;
        b.size = header + static_cast<size_t>(size);
        bool whole = true;
        for (size_t e = at + header; e < at + b.size;) {
            const uint32_t entry_header = Read<uint32_t>(dll, e + 4), payload = Read<uint32_t>(dll, e + 8);
            if (entry_header < 64 || entry_header > 1024 || e + entry_header + payload > at + b.size) {
                whole = false;
                break;
            }
            b.entries.push_back({Read<uint16_t>(dll, e), Read<uint32_t>(dll, e + 28), e + entry_header, payload, Read<uint64_t>(dll, e + 56)});
            e += entry_header + payload;
        }
        if (!whole || b.entries.empty()) {
            at += 4;
            continue;
        }
        out.push_back(std::move(b));
        at += out.back().size;
    }
    return out;
}

uint64_t BundleHash(std::span<const uint8_t> bytes) {
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t b : bytes) hash = (hash ^ b) * 1099511628211ull;
    return hash;
}

bool ExtractCode(std::span<const uint8_t> runtime_dll, std::span<const uint8_t> code_dll, int arch, const std::filesystem::path& out,
                 std::string* detail) {
#ifdef _WIN32
    auto fail = [&](const std::string& why) {
        if (detail) *detail = why;
        return false;
    };
    const std::vector<Bundle> wanted = FindBundles(runtime_dll), source = FindBundles(code_dll);
    if (wanted.size() != kBundles || source.size() != kBundles) {
        return fail(std::format("the runtimes have {} and {} GPU code bundles, not {}", wanted.size(), source.size(), kBundles));
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    std::ofstream list(out / "bundles.txt", std::ios::binary | std::ios::trunc);
    for (size_t i = 0; i < kBundles; ++i) {
        const auto& entries = source[i].entries;
        const auto it = std::find_if(entries.begin(), entries.end(), [&](const Bundle::Entry& e) { return e.kind == 2 && e.arch == static_cast<uint32_t>(arch); });
        if (it == entries.end()) return fail(std::format("bundle {} has no sm_{} code", i, arch));
        const std::span<const uint8_t> packed = code_dll.subspan(it->offset, it->size);
        std::vector<uint8_t> code;
        if (packed.size() >= 4 && Read<uint32_t>(packed, 0) == 0xFD2FB528u) {
            if (it->unpacked == 0 || it->unpacked > 64ull * 1024 * 1024) return fail(std::format("bundle {} records no size to unpack", i));
            code.resize(static_cast<size_t>(it->unpacked));
            const size_t frame = ZSTD_findFrameCompressedSize(packed.data(), packed.size());
            if (ZSTD_isError(frame)) return fail(std::format("bundle {}: zstd {}", i, ZSTD_getErrorName(frame)));
            const size_t got = ZSTD_decompress(code.data(), code.size(), packed.data(), frame);
            if (ZSTD_isError(got) || got != code.size()) {
                return fail(std::format("bundle {}: zstd {}", i, ZSTD_isError(got) ? ZSTD_getErrorName(got) : "gave the wrong size"));
            }
        } else {
            code.assign(packed.begin(), packed.end());
        }
        if (code.size() < 4 || std::memcmp(code.data(), "\x7f" "ELF", 4) != 0) return fail(std::format("bundle {}: the sm_{} code is not an ELF", i, arch));
        const uint64_t hash = BundleHash(runtime_dll.subspan(wanted[i].offset, wanted[i].size));
        const std::string name = std::format("{:016x}.cubin", hash);
        std::ofstream file(out / name, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(code.data()), static_cast<std::streamsize>(code.size()));
        if (!file) return fail("cannot write " + os::PathToUtf8(out / name));
        list << name << ' ' << code.size() << '\n';
    }
    list.close();
    return list ? true : fail("cannot write the bundle list");
#else
    (void)runtime_dll;
    (void)code_dll;
    (void)arch;
    (void)out;
    if (detail) *detail = "Windows only";
    return false;
#endif
}

bool InstallTo(const std::filesystem::path& data_dir, int arch, const std::function<void(int)>& percent,
               const std::function<bool()>& cancelled, std::string* failure_key, std::string* detail) {
#ifdef _WIN32
    auto fail = [&](const char* key, const std::string& why) {
        if (failure_key) *failure_key = key;
        if (detail) *detail = why;
        LogWarn("dlss5 native: {}", why);
        return false;
    };
    if (arch < 0) return fail("pc_note_nr_gpu", "this card has no DLSS 5 (an NVIDIA GeForce RTX is needed)");
    if (Read(data_dir, arch)) {
        LogInfo("dlss5 native: {} is already installed{}", kRuntimeVersion, arch > 0 ? std::format(" with the sm_{} code", arch) : std::string());
        return true;
    }
    const std::filesystem::path dir = VersionDir(data_dir), work = Root(data_dir) / "download";
    std::error_code ec;
    std::filesystem::remove(Root(data_dir) / std::string(kRemoveMarker), ec);
    std::filesystem::create_directories(dir, ec);
    std::filesystem::create_directories(work, ec);
    if (ec) return fail("pc_note_nr_unlock_failed", "cannot make " + os::PathToUtf8(dir) + ": " + ec.message());
    const uint64_t need = kRuntimeSize + (arch > 0 ? kCodeZipSize + 170ull * 1024 * 1024 + 64ull * 1024 * 1024 : 0) + 64ull * 1024 * 1024;
    const auto space = std::filesystem::space(dir, ec);
    if (!ec && space.available < need) {
        return fail("pc_note_nr_unlock_space", std::format("{} MB free in {}, {} MB needed", space.available >> 20, os::PathToUtf8(dir), need >> 20));
    }
    const int runtime_share = arch > 0 ? 60 : 100;
    const std::filesystem::path runtime = dir / std::string(kRuntimeFile);
    if (percent && SizeOf(runtime) == kRuntimeSize) percent(100);
    if (SizeOf(runtime) != kRuntimeSize || nr_unlock::FileSha256(runtime) != kRuntimeSha256) {
        const std::string url = EnvOr("PT_NR_RUNTIME_URL", kRuntimeUrl);
        LogInfo("dlss5 native: fetching NVIDIA's runtime {} from {}", kRuntimeVersion, url);
        std::string error;
        const std::string sha = nr_unlock::DownloadFile(url, runtime, kRuntimeSize + 1024 * 1024,
                                                        [&](int p) { if (percent) percent(p * runtime_share / 100); }, cancelled, &error);
        if (sha.empty()) return fail("pc_note_nr_unlock_download", "the runtime could not be downloaded: " + error);
        if (sha != kRuntimeSha256 || SizeOf(runtime) != kRuntimeSize) {
            std::filesystem::remove(runtime, ec);
            return fail("pc_note_nr_unlock_hash", std::format("the runtime's SHA-256 {} ({} bytes) is not {} ({} bytes)", sha, SizeOf(runtime), kRuntimeSha256, kRuntimeSize));
        }
    }
    if (percent) percent(runtime_share);
    if (arch > 0) {
        const std::filesystem::path code = CodeDir(dir, arch), partial = dir / std::format("sm_{}.partial", arch);
        if (!CodeComplete(code)) {
            const std::string url = EnvOr("PT_NR_CODE_URL", kCodeUrl);
            const std::filesystem::path zip = work / "dlssnr-code.zip";
            if (percent && SizeOf(zip) == kCodeZipSize) percent(100);
            if (SizeOf(zip) != kCodeZipSize || nr_unlock::FileSha256(zip) != kCodeZipSha256) {
                LogInfo("dlss5 native: fetching the community build's GPU code (sm_{}) from {}", arch, url);
                std::string error;
                const std::string sha = nr_unlock::DownloadFile(url, zip, kCodeZipSize + 1024 * 1024,
                                                                [&](int p) { if (percent) percent(runtime_share + p * 35 / 100); }, cancelled, &error);
                if (sha.empty()) return fail("pc_note_nr_unlock_download", "the GPU code could not be downloaded: " + error);
                if (sha != kCodeZipSha256 || SizeOf(zip) != kCodeZipSize) {
                    std::filesystem::remove(zip, ec);
                    return fail("pc_note_nr_unlock_hash", std::format("the GPU code zip's SHA-256 {} is not {}", sha, kCodeZipSha256));
                }
            }
            if (percent) percent(100);
            wchar_t system[MAX_PATH] = {};
            GetSystemDirectoryW(system, MAX_PATH);
            const std::filesystem::path unpacked = work / std::string(kRuntimeFile);
            std::filesystem::remove(unpacked, ec);
            std::atomic<bool> never{false};
            const os::ProcessResult tar = os::RunProcess(std::filesystem::path(system) / L"tar.exe",
                                                         {"-xf", os::PathToUtf8(zip), "-C", os::PathToUtf8(work), std::string(kRuntimeFile)}, work,
                                                         work / "tar.log", never, std::chrono::minutes(3));
            if (!tar.started || tar.exit_code != 0) return fail("pc_note_nr_unlock_failed", std::format("the GPU code zip could not be unpacked (tar started {}, exit {})", tar.started, tar.exit_code));
            if (const std::string sha = nr_unlock::FileSha256(unpacked); sha != kCodeDllSha256) {
                std::filesystem::remove(unpacked, ec);
                return fail("pc_note_nr_unlock_hash", std::format("the community DLL's SHA-256 {} is not {}", sha.empty() ? "unreadable" : sha, kCodeDllSha256));
            }
            std::string why;
            {
                const Mapped now(runtime), old(unpacked);
                if (!now.Ok() || !old.Ok()) return fail("pc_note_nr_unlock_failed", "the runtimes could not be read");
                std::filesystem::remove_all(partial, ec);
                if (!ExtractCode(now.Bytes(), old.Bytes(), arch, partial, &why)) {
                    std::filesystem::remove_all(partial, ec);
                    return fail("pc_note_nr_unlock_failed", "the GPU code could not be taken out: " + why);
                }
            }
            std::filesystem::remove_all(code, ec);
            std::filesystem::rename(partial, code, ec);
            if (ec || !CodeComplete(code)) return fail("pc_note_nr_unlock_failed", "the GPU code could not be put in place: " + ec.message());
            LogInfo("dlss5 native: sm_{} GPU code in place ({} bundles)", arch, kBundles);
        }
    }
    std::filesystem::remove_all(work, ec);
    {
        std::ofstream record(dir / std::string(kRecord), std::ios::binary | std::ios::trunc);
        record << "{\"version\":\"" << kRuntimeVersion << "\",\"runtime_sha256\":\"" << kRuntimeSha256 << "\",\"code_source\":\"" << kCodeUrl
               << "\",\"code_sha256\":\"" << kCodeZipSha256 << "\"}\n";
        if (!record) return fail("pc_note_nr_unlock_failed", "cannot write the install record");
    }
    if (percent) percent(100);
    if (!Read(data_dir, arch)) return fail("pc_note_nr_unlock_failed", "the install does not read back complete");
    LogInfo("dlss5 native: {} installed in {}", kRuntimeVersion, os::PathToUtf8(dir));
    return true;
#else
    (void)data_dir;
    (void)arch;
    (void)percent;
    (void)cancelled;
    if (failure_key) *failure_key = "pc_note_nr_unlock_failed";
    if (detail) *detail = "Windows only";
    return false;
#endif
}

bool RemoveFrom(const std::filesystem::path& data_dir, bool* deferred, std::string* detail) {
    if (deferred) *deferred = false;
    const std::filesystem::path root = Root(data_dir);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return true;
#ifdef _WIN32
    const std::filesystem::path runtime = VersionDir(data_dir) / std::string(kRuntimeFile);
    if (GetModuleHandleW(runtime.c_str()) != nullptr) {
        std::ofstream(root / std::string(kRemoveMarker)) << "remove\n";
        if (deferred) *deferred = true;
        LogInfo("dlss5 native: the runtime is loaded; it is removed at the next start");
        return false;
    }
#endif
    std::filesystem::remove_all(root, ec);
    if (ec) {
        std::ofstream(root / std::string(kRemoveMarker)) << "remove\n";
        if (deferred) *deferred = true;
        if (detail) *detail = ec.message();
        LogWarn("dlss5 native: remove failed now ({}); it is retried at the next start", ec.message());
        return false;
    }
    LogInfo("dlss5 native: removed");
    return true;
}

bool Job::Start(Kind kind, const std::filesystem::path& data_dir, int arch) {
    if (state_->busy.exchange(true)) return false;
    {
        std::lock_guard lock(state_->mutex);
        const uint32_t serial = state_->status.serial;
        state_->status = Status{};
        state_->status.kind = kind;
        state_->status.phase = Phase::Working;
        state_->status.serial = serial;
    }
    std::thread([state = state_, kind, data_dir, arch] {
        os::LowerCurrentThreadPriority();
        std::string failure, detail;
        bool ok = false, deferred = false;
        if (kind == Kind::Reinstall) {
            std::error_code ec;
            const std::filesystem::path dir = VersionDir(data_dir);
            std::filesystem::remove(dir / std::string(kRecord), ec);
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
                if (entry.is_directory(ec) && os::PathToUtf8(entry.path().filename()).rfind("sm_", 0) == 0) std::filesystem::remove_all(entry.path(), ec);
        }
        if (kind != Kind::Remove) {
            ok = InstallTo(data_dir, arch, [&](int p) { std::lock_guard lock(state->mutex); state->status.percent = std::clamp(p, 0, 100); },
                           [&] { return state->cancel.load(); }, &failure, &detail);
        } else {
            ok = RemoveFrom(data_dir, &deferred, &detail) || deferred;
        }
        std::lock_guard lock(state->mutex);
        state->status.phase = ok ? Phase::Done : Phase::Failed;
        state->status.percent = ok ? 100 : state->status.percent;
        state->status.deferred = deferred;
        state->status.failure = ok ? std::string() : (failure.empty() ? "pc_note_nr_unlock_failed" : failure);
        state->status.detail = detail;
        ++state->status.serial;
        state->busy = false;
    }).detach();
    return true;
}

Job::Status Job::Get() const {
    std::lock_guard lock(state_->mutex);
    return state_->status;
}

bool Job::Busy() const { return state_->busy.load(); }

}
