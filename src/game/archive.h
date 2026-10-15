#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace pt::game {

enum class ArchiveSection : uint8_t { Images, Crash, Radio, Voices, Photos, Cutscenes, Models, Music, Unused, GamePlus };
inline constexpr int kArchiveSectionCount = 10;

enum class ArchiveMedia : uint8_t {
    Image,
    String,
    Screen,
    Sound,
    Dialogue,
    Photo,
    Demo,
    Model,
    Scene,
    Music,
};

struct ArchiveEntry {
    std::string_view id;
    ArchiveSection section = ArchiveSection::Images;
    ArchiveMedia media = ArchiveMedia::Image;
    std::string_view label;
    int number = 0;
    std::string_view asset;
    std::string_view extra;
    std::string_view floor;
    std::string_view unlock;
    std::string_view note;
    std::string_view hint;
    int hint_loop = 0;
    int hint_loop2 = 0;
    bool unused = false;
    bool secret = false;
    bool free_camera = false;
    float free_offset[3] = {0.0f, 0.0f, 0.0f};
    float seconds = 0.0f;
    float loop_from = 0.0f;
};

std::span<const ArchiveEntry> ArchiveEntries();
const ArchiveEntry* FindArchiveEntry(std::string_view id);
std::string ArchiveUnlockKey(const ArchiveEntry& entry);
bool IsArchiveUnlockKey(std::string_view key);
std::string ArchiveBugScreenKey(std::string_view texture_stem);
std::string ArchiveHint(const ArchiveEntry& entry);
inline constexpr std::string_view kArchiveLockedLabel = "???";
struct ArchiveTally {
    int found = 0;
    int total = 0;
    int bonus = 0;
};
template <typename Open>
ArchiveTally ArchiveCount(std::span<const ArchiveEntry> entries, int section, Open&& open) {
    ArchiveTally tally;
    for (const ArchiveEntry& e : entries) {
        if (section >= 0 && static_cast<int>(e.section) != section) continue;
        const bool is_open = open(e);
        if (e.secret) {
            tally.bonus += is_open ? 1 : 0;
            continue;
        }
        ++tally.total;
        tally.found += is_open ? 1 : 0;
    }
    return tally;
}
inline int ArchivePercent(const ArchiveTally& tally) {
    return tally.total > 0 ? tally.found * 100 / tally.total : 100;
}
std::span<const std::string_view> ArchiveFinishKeys();
std::string_view ArchiveSectionTitle(ArchiveSection section);
std::string_view ArchiveSectionNote(ArchiveSection section);
std::string ArchiveLabel(const ArchiveEntry& entry, int language);
std::string ArchiveCaption(const ArchiveEntry& entry);
std::string_view ArchivePictureOf(const ArchiveEntry& entry);
std::string_view ArchiveSectionCover(ArchiveSection section);
int ArchiveThumbnailFrames(const ArchiveEntry& entry);

}
