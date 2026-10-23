#include "engine/fs/data_pack.h"

#include <zlib.h>

#include <cstring>
#include <fstream>
#include <limits>

#include "engine/core/log.h"
#include "engine/core/resource_path.h"
#include "engine/platform/os.h"

namespace pt {
namespace {

constexpr char kMagic[4] = {'P', 'T', 'D', 'P'};
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 24;
constexpr uint64_t kIndexSeed = 0x6A09E667F3BCC908ull;
constexpr uint64_t kEntrySeed = 0xBB67AE8584CAA73Bull;
constexpr uint32_t kMaxRaw = 512u << 20;

uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

uint64_t U64(const uint8_t* p) { return static_cast<uint64_t>(U32(p)) | static_cast<uint64_t>(U32(p + 4)) << 32; }

bool Inflate(const std::vector<uint8_t>& stored, uint32_t raw, uint32_t crc, std::vector<uint8_t>& out) {
    out.clear();
    if (raw == 0) return crc == 0;
    out.resize(raw);
    uLongf length = raw;
    const int z = uncompress(out.data(), &length, stored.data(), static_cast<uLong>(stored.size()));
    if (z != Z_OK || length != raw) return false;
    return crc32(0L, out.data(), static_cast<uInt>(out.size())) == crc;
}

bool ReadAt(std::ifstream& in, uint64_t offset, std::vector<uint8_t>& out) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    if (!in) return false;
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<size_t>(in.gcount()) == out.size();
}

}

void DataPack::Mask(uint8_t* data, size_t size, uint64_t seed) {
    uint64_t s = seed;
    for (size_t i = 0; i < size; i += 8) {
        s += 0x9E3779B97F4A7C15ull;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        const size_t n = size - i < 8 ? size - i : 8;
        for (size_t b = 0; b < n; ++b) data[i + b] ^= static_cast<uint8_t>(z >> (8 * b));
    }
}

uint64_t DataPack::EntrySeed(std::string_view name) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (const char c : name) h = (h ^ static_cast<uint8_t>(c)) * 0x100000001B3ull;
    return kEntrySeed ^ h;
}

bool DataPack::Open(const std::filesystem::path& path, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        entries_.clear();
        path_.clear();
        return false;
    };
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("not found");
    std::vector<uint8_t> header(kHeaderSize);
    if (!ReadAt(in, 0, header) || std::memcmp(header.data(), kMagic, 4) != 0) return fail("not a data pack");
    if (U32(&header[4]) != kVersion) return fail("unknown version");
    const uint32_t count = U32(&header[8]), stored_size = U32(&header[12]), raw_size = U32(&header[16]), crc = U32(&header[20]);
    if (stored_size > kMaxRaw || raw_size > kMaxRaw) return fail("damaged index");
    std::vector<uint8_t> stored(stored_size);
    if (!ReadAt(in, kHeaderSize, stored)) return fail("short file");
    Mask(stored.data(), stored.size(), kIndexSeed);
    std::vector<uint8_t> index;
    if (!Inflate(stored, raw_size, crc, index)) return fail("index failed its check");
    in.seekg(0, std::ios::end);
    const uint64_t file_size = static_cast<uint64_t>(in.tellg());
    std::unordered_map<std::string, Entry> entries;
    size_t pos = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 2 > index.size()) return fail("damaged index");
        const size_t length = static_cast<size_t>(index[pos]) | static_cast<size_t>(index[pos + 1]) << 8;
        if (pos + 2 + length + 20 > index.size()) return fail("damaged index");
        std::string name(reinterpret_cast<const char*>(&index[pos + 2]), length);
        const uint8_t* r = &index[pos + 2 + length];
        Entry e{U64(r), U32(r + 8), U32(r + 12), U32(r + 16)};
        pos += 2 + length + 20;
        if (e.raw > kMaxRaw || e.offset > file_size || e.stored > file_size - e.offset) return fail("damaged index");
        entries.emplace(std::move(name), e);
    }
    entries_ = std::move(entries);
    path_ = path;
    return true;
}

bool DataPack::Contains(std::string_view name) const { return entries_.contains(std::string(name)); }

std::vector<std::string> DataPack::Names() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto& [name, entry] : entries_) names.push_back(name);
    return names;
}

std::optional<std::vector<uint8_t>> DataPack::Read(std::string_view name) const {
    const auto it = entries_.find(std::string(name));
    if (it == entries_.end()) return std::nullopt;
    const Entry& e = it->second;
    std::vector<uint8_t> stored(e.stored);
    {
        std::lock_guard lock(mutex_);
        std::ifstream in(path_, std::ios::binary);
        if (!in || !ReadAt(in, e.offset, stored)) {
            LogWarn("data pack: {} could not be read at {}", pt::os::PathToUtf8(path_.filename()), e.offset);
            return std::nullopt;
        }
    }
    Mask(stored.data(), stored.size(), EntrySeed(name));
    std::vector<uint8_t> out;
    if (!Inflate(stored, e.raw, e.crc, out)) {
        LogWarn("data pack: the entry at {} of {} failed its check (damaged file)", e.offset, pt::os::PathToUtf8(path_.filename()));
        return std::nullopt;
    }
    return out;
}

const DataPack& ExtrasPack() {
    static const DataPack* pack = [] {
        auto* p = new DataPack();
        const std::filesystem::path path = ExecutableDir() / kExtrasPackName;
        std::string error;
        if (p->Open(path, &error)) {
            LogInfo("data pack: {}, {} entries", pt::os::PathToUtf8(path), p->Count());
        } else {
            LogWarn("data pack: {} not opened ({}); the files it holds are missing", pt::os::PathToUtf8(path), error);
        }
        return p;
    }();
    return *pack;
}

}
