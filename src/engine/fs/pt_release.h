#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace pt::release {
namespace fs = std::filesystem;

struct KnownTitle {
    const char* title_id;
    const char* region;
    bool confirmed;
};
inline constexpr KnownTitle kKnownTitles[] = {
    {"CUSA01127", "US", true},
    {"CUSA01114", "Europe", true},
    {"CUSA01098", "Japan", false},
};

inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
inline char Upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }
inline bool IsAlnum(char c) { return IsDigit(c) || (Upper(c) >= 'A' && Upper(c) <= 'Z'); }

inline bool IsTitleId(std::string_view text) {
    if (text.size() != 9) return false;
    for (size_t i = 0; i < 4; ++i)
        if (text[i] < 'A' || text[i] > 'Z') return false;
    for (size_t i = 4; i < 9; ++i)
        if (!IsDigit(text[i])) return false;
    return true;
}
inline std::string TitleIdOfName(std::string_view name) {
    if (name.size() < 9) return {};
    std::string head;
    for (size_t i = 0; i < 9; ++i) head += Upper(name[i]);
    if (!IsTitleId(head)) return {};
    if (name.size() > 9 && IsAlnum(name[9])) return {};
    return head;
}
inline const KnownTitle* FindKnownTitle(std::string_view title_id) {
    for (const KnownTitle& title : kKnownTitles)
        if (title_id == title.title_id) return &title;
    return nullptr;
}

inline const char* RegionOfContentId(std::string_view content_id) {
    if (content_id.size() < 7 || content_id[1] != 'P' || content_id[6] != '-') return nullptr;
    switch (content_id[0]) {
        case 'U': return "US";
        case 'E': return "Europe";
        case 'J': return "Japan";
        case 'H': return "Asia";
        case 'K': return "Korea";
        default: return nullptr;
    }
}
inline std::string TitleOfContentId(std::string_view content_id) {
    if (content_id.size() < 16 || content_id[6] != '-') return {};
    const std::string_view title = content_id.substr(7, 9);
    return IsTitleId(title) ? std::string(title) : std::string();
}

struct Identity {
    std::string title_id, content_id, region;
    std::string how;
    bool known = false;
    bool confirmed = false;
};

inline std::string ParamSfoValue(const std::string& data, const char* wanted) {
    auto u32 = [&](size_t at) -> uint32_t {
        if (at + 4 > data.size()) return 0;
        uint32_t v;
        std::memcpy(&v, data.data() + at, 4);
        return v;
    };
    auto u16 = [&](size_t at) -> uint16_t {
        if (at + 2 > data.size()) return 0;
        uint16_t v;
        std::memcpy(&v, data.data() + at, 2);
        return v;
    };
    if (data.size() < 0x14 || data.compare(0, 4, std::string("\0PSF", 4)) != 0) return {};
    const uint32_t keys = u32(8), values = u32(12), count = u32(16);
    for (uint32_t i = 0; i < count && i < 512; ++i) {
        const size_t entry = 0x14 + size_t(i) * 16;
        if (entry + 16 > data.size()) break;
        const size_t key = size_t(keys) + u16(entry), value = size_t(values) + u32(entry + 12), length = u32(entry + 4);
        if (key >= data.size() || value > data.size() || length > data.size() - value) continue;
        if (std::string_view(data.c_str() + key) != wanted) continue;
        std::string text = data.substr(value, length);
        text.erase(std::find(text.begin(), text.end(), '\0'), text.end());
        return text;
    }
    return {};
}
inline std::string ReadSmallFile(const fs::path& file, uintmax_t limit) {
    std::error_code error;
    if (!fs::is_regular_file(file, error)) return {};
    const auto size = fs::file_size(file, error);
    if (error || size > limit) return {};
    std::ifstream input(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), {});
}

inline Identity Identify(const fs::path& folder) {
    Identity id;
    auto finish = [&]() {
        if (id.region.empty()) {
            if (const char* region = RegionOfContentId(id.content_id)) id.region = region;
        }
        if (id.title_id.empty()) id.title_id = TitleOfContentId(id.content_id);
        if (const KnownTitle* known = FindKnownTitle(id.title_id)) {
            id.known = true;
            id.confirmed = known->confirmed;
            if (id.region.empty()) id.region = known->region;
        }
        if (id.region.empty() && !id.title_id.empty()) id.region = "unknown";
        return id;
    };
    const std::string source = ReadSmallFile(folder / "source.txt", 64 * 1024);
    if (!source.empty()) {
        size_t at = 0;
        while (at < source.size()) {
            size_t end = source.find('\n', at);
            if (end == std::string::npos) end = source.size();
            std::string line = source.substr(at, end - at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with("title_id=")) id.title_id = line.substr(9);
            else if (line.starts_with("content_id=")) id.content_id = line.substr(11);
            else if (line.starts_with("region=") && line != "region=unknown") id.region = line.substr(7);
            at = end + 1;
        }
        if (!id.title_id.empty() || !id.content_id.empty()) {
            id.how = "source.txt";
            return finish();
        }
        id = Identity{};
    }
    for (const fs::path& dir : {folder, folder.parent_path()}) {
        if (dir.empty()) continue;
        const std::string sfo = ReadSmallFile(dir / "sce_sys" / "param.sfo", 1024 * 1024);
        if (sfo.empty()) continue;
        id.title_id = ParamSfoValue(sfo, "TITLE_ID");
        id.content_id = ParamSfoValue(sfo, "CONTENT_ID");
        if (!id.title_id.empty() || !id.content_id.empty()) {
            id.how = "sce_sys/param.sfo";
            return finish();
        }
    }
    id.title_id = TitleIdOfName(folder.filename().string());
    if (!id.title_id.empty()) id.how = "the folder name";
    return finish();
}

inline std::string ExtractionFolderName(std::string_view package_title_id) {
    return IsTitleId(package_title_id) ? std::string(package_title_id) : std::string("PT-game-files");
}

}
