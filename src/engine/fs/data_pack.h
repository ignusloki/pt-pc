#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pt {

class DataPack {
public:
    DataPack() = default;
    DataPack(const DataPack&) = delete;
    DataPack& operator=(const DataPack&) = delete;

    bool Open(const std::filesystem::path& path, std::string* error = nullptr);
    bool IsOpen() const { return !path_.empty(); }
    bool Contains(std::string_view name) const;
    std::optional<std::vector<uint8_t>> Read(std::string_view name) const;
    std::vector<std::string> Names() const;
    size_t Count() const { return entries_.size(); }

    static void Mask(uint8_t* data, size_t size, uint64_t seed);
    static uint64_t EntrySeed(std::string_view name);

private:
    struct Entry {
        uint64_t offset = 0;
        uint32_t stored = 0;
        uint32_t raw = 0;
        uint32_t crc = 0;
    };
    std::filesystem::path path_;
    std::unordered_map<std::string, Entry> entries_;
    mutable std::mutex mutex_;
};

const DataPack& ExtrasPack();
inline constexpr const char* kExtrasPackName = "extras.pak";

inline constexpr std::string_view kPackPathPrefix = "pack:";
inline bool IsPackPath(std::string_view path) { return path.starts_with(kPackPathPrefix); }
inline std::string PackPath(std::string_view entry) { return std::string(kPackPathPrefix) + std::string(entry); }
inline std::string_view PackEntryOf(std::string_view path) { return path.substr(kPackPathPrefix.size()); }

}
