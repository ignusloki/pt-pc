#include "engine/audio/wwise_bank.h"
#include "game/archive.h"
#include "game/loop_browser.h"
#include "game/ui/demo_ui.h"
#include "game/ui/pc_settings.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failed = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    failed += !ok;
}

const pt::game::ArchiveEntry& Entry(std::string_view id) {
    static const pt::game::ArchiveEntry none{};
    const pt::game::ArchiveEntry* e = pt::game::FindArchiveEntry(id);
    if (!e) Check(false, "entry " + std::string(id) + " exists");
    return e ? *e : none;
}

std::set<std::string> Opened(std::string_view key) {
    std::set<std::string> out;
    for (const pt::game::ArchiveEntry& e : pt::game::ArchiveEntries()) {
        if (pt::game::ArchiveUnlockKey(e) == key) out.insert(std::string(e.id));
    }
    return out;
}

}

int main() {
    using pt::game::ArchiveSection;
    for (const pt::game::ArchiveEntry& e : pt::game::ArchiveEntries()) {
        if (e.media != pt::game::ArchiveMedia::Sound) continue;
        const std::string key = pt::game::ArchiveUnlockKey(e);
        if (!key.starts_with("voice:")) continue;
        const std::string subtitle = key.substr(6);
        int shared = 0;
        for (const pt::game::ArchiveEntry& other : pt::game::ArchiveEntries()) {
            shared += other.id != e.id && other.media == pt::game::ArchiveMedia::Sound && other.extra == subtitle;
        }
        Check(shared == 0, std::string(e.id) + "'s key " + key + " is no other sound's transcript");
    }
    Check(Opened("voice:tria1000_151010").empty(), "the peephole broadcast's marker subtitle opens nothing");
    Check(Opened("event:Play_Peephole_Theater") == std::set<std::string>{"peephole_voice"}, "the peephole broadcast opens its own exhibit");
    Check(Opened("event:Play_radio_riddle_01") == std::set<std::string>{"riddle_1"}, "riddle 1 waits for its own event");
    Check(Entry("riddle_1").extra == Entry("peephole_voice").extra, "riddle 1 keeps the subtitle its marker links in the original data");

    for (const char* id : {"radio_01", "radio_02", "radio_03", "radio_05", "riddle_1", "riddle_2"}) {
        Check(Entry(id).section == ArchiveSection::Radio && Entry(id).unused, std::string(id) + " is in the radio hall and opens with the finish");
    }
    for (const char* id : {"radio_04", "radio_06", "radio_07", "radio_08", "radio_news", "radio_news2", "sermon", "peephole_voice"}) {
        Check(Entry(id).section == ArchiveSection::Radio && !Entry(id).unused, std::string(id) + " is in the radio hall");
    }
    for (const char* id : {"phone", "bag", "fetus_1", "fetus_2"}) {
        Check(Entry(id).section == ArchiveSection::Voices, std::string(id) + " is in the voices hall");
    }
    std::set<std::string> unused;
    for (const pt::game::ArchiveEntry& e : pt::game::ArchiveEntries()) {
        if (e.unused) unused.insert(std::string(e.id));
    }
    Check(unused == std::set<std::string>{"radio_01", "radio_02", "radio_03", "radio_05", "riddle_1", "riddle_2", "bathtub_lisa", "crash_7",
                                          "coffee_lisa"},
          "the entries that open with the finish are the former unused hall's");
    Check(Opened("voice:tria1000_1d1010") == std::set<std::string>{"radio_06"}, "radio line 06's marker opens radio_06");
    Check(Opened("voice:tria1000_1e1010") == std::set<std::string>{"radio_07"}, "radio line 07's marker opens radio_07");
    Check(Opened("voice:tria1000_1f1010") == std::set<std::string>{"radio_08"}, "radio line 08's marker opens radio_08");
    Check(Opened("voice:tria1000_1b1010") == std::set<std::string>{"radio_04"}, "radio line 04's marker opens radio_04");
    const std::string arg(1, pt::game::kPcNoteArgument);
    for (const char* id : {"radio_06", "radio_07", "radio_08"}) {
        Check(pt::game::ArchiveHint(Entry(id)) == "pc_note_archive_hint_radio_wait" + arg + "7" + arg + "8",
              std::string(id) + " says it plays on the radio of loops 7 and 8 in Game+ 3 while locked");
    }

    for (int n = 1; n <= 10; ++n) {
        const std::string id = n < 10 ? "sub0" + std::to_string(n) : "sub10";
        Check(Opened("sub:" + std::to_string(n)) == std::set<std::string>{id}, "sub:" + std::to_string(n) + " opens " + id);
        Check(pt::game::ArchiveHint(Entry(id)) == (n <= 4 ? "pc_note_archive_hint_photo_flash" : "pc_note_archive_hint_random_final"),
              id + "'s hint");
    }

    const std::map<uint32_t, std::string> crash = {{0, "crash_1"}, {1, "crash_2"}, {2, "crash_3"}, {3, "crash_4"},
                                                   {4, "crash_5"}, {5, "crash_6"}, {6, "crash_7"}};
    for (const auto& [pick, id] : crash) {
        const std::string_view texture = pt::game::DemoUi::BugPictureTexture(pick);
        const std::string key = pt::game::ArchiveBugScreenKey(texture);
        Check(pt::game::IsArchiveUnlockKey(key), key + " is an unlock key");
        Check(Opened(key) == std::set<std::string>{id}, "bug case " + std::to_string(pick) + " (" + std::string(texture) + ") opens " + id);
        const pt::game::ArchiveEntry& e = Entry(id);
        Check(e.media == pt::game::ArchiveMedia::Screen && e.asset.find(std::string(texture) + ".ftex") != std::string_view::npos,
              id + " shows " + std::string(texture));
        Check(e.section == ArchiveSection::Crash && e.unused == (pick == 6), id + " is in the crash screens hall" + (pick == 6 ? ", opening with the finish" : ""));
        Check(pt::game::ArchiveHint(e) == (pick < 6 ? "pc_note_archive_hint_crash" + arg + "14" : std::string("pc_note_archive_hint_unused")),
              id + "'s hint names the fake crash loop, 14 (or the finish)");
    }
    Check(pt::game::DemoUi::BugPictureTexture(7).empty(), "no eighth bug picture");

    for (const pt::game::ArchiveEntry& e : pt::game::ArchiveEntries()) {
        Check(pt::game::IsArchiveUnlockKey(pt::game::ArchiveUnlockKey(e)), std::string(e.id) + "'s key is kept");
        const std::string hint = pt::game::ArchiveHint(e);
        const std::string_view hint_key = std::string_view(hint).substr(0, hint.find(pt::game::kPcNoteArgument));
        for (const std::string_view text : {e.label, e.note, hint_key}) {
            Check(!text.empty() && pt::game::PcText(text, 0) != text, std::string(e.id) + " text " + std::string(text));
        }
        const std::string label = pt::game::ArchiveLabel(e, 0);
        for (const char* id : {"sub0", "sub1", "sh_bug", "media", "gc_p", "f0", "f1", "_"}) {
            Check(label.find(id) == std::string::npos, std::string(e.id) + "'s name \"" + label + "\" has no " + id);
        }
        const std::string hint_text(pt::game::PcText(hint_key, 0));
        const int args = (e.hint_loop > 0) + (e.hint_loop2 > 0);
        Check((hint_text.find("{1}") != std::string::npos) == (args >= 1) && (hint_text.find("{2}") != std::string::npos) == (args >= 2),
              std::string(e.id) + "'s hint takes " + std::to_string(args) + " loop(s)");
        for (const int loop : {e.hint_loop, e.hint_loop2}) {
            Check(loop >= 0 && loop <= 15, std::string(e.id) + "'s hint loop " + std::to_string(loop) + " is a numbered loop");
        }
        Check(pt::game::PcNoteText(hint, 0).find("f0") == std::string::npos && pt::game::PcNoteText(hint, 0).find("f1") == std::string::npos,
              std::string(e.id) + "'s hint names no floor code: " + pt::game::PcNoteText(hint, 0));
        if (e.media == pt::game::ArchiveMedia::Demo && e.hint_loop > 0 && e.floor != "f050" && e.id != "gc_p01_010") {
            Check(pt::game::BrowseIndexOf(e.floor, 1) == e.hint_loop, std::string(e.id) + "'s hint loop is its floor's loop browser entry");
        }
    }
    Check(pt::game::BrowseIndexOf("f050", 1) == 7 && pt::game::BrowseIndexOf("f050", 2) == 8 && pt::game::BrowseIndexOf("f120", 1) == 14 &&
              pt::game::BrowseIndexOf("f160", 1) == 15,
          "loop numbers: f050 is 7 and 8, the fake crash 14, the final loop 15");
    std::vector<std::string> museum_keys = {"pc_archive_hall_crash", "pc_archive_hall_radio", "pc_archive_hall_voices", "pc_archive_hall_gameplus",
                                            "pc_note_archive_crash_section", "pc_note_archive_radio_section", "pc_note_archive_voices_section",
                                            "pc_note_archive_gameplus_section", "pc_archive_found", "pc_archive_bonus", "pc_archive_complete",
                                            "pc_archive_music_unrest03", "pc_archive_music_herald", "pc_archive_music_unrest02", "pc_archive_music_f110"};
    for (const pt::game::ArchiveEntry& e : pt::game::ArchiveEntries()) {
        museum_keys.emplace_back(e.label);
        const std::string hint = pt::game::ArchiveHint(e);
        museum_keys.emplace_back(hint.substr(0, hint.find(pt::game::kPcNoteArgument)));
    }
    std::sort(museum_keys.begin(), museum_keys.end());
    museum_keys.erase(std::unique(museum_keys.begin(), museum_keys.end()), museum_keys.end());
    for (const std::string& key : museum_keys) {
        for (int language = 1; language < 14; ++language) {
            const std::string_view text = pt::game::PcText(key, language);
            const bool numbered = pt::game::PcText(key, 0).find("{1}") != std::string_view::npos;
            const bool same_ok = key == "pc_archive_radio_204863" || key == "pc_archive_man" || key == "pc_archive_bonus";
            Check(text != key && (text != pt::game::PcText(key, 0) || same_ok) &&
                      (!numbered || text.find("{1}") != std::string_view::npos),
                  key + " is translated in language " + std::to_string(language));
        }
    }

    const auto entries = pt::game::ArchiveEntries();
    const auto none = pt::game::ArchiveCount(entries, -1, [](const pt::game::ArchiveEntry&) { return false; });
    const auto every = pt::game::ArchiveCount(entries, -1, [](const pt::game::ArchiveEntry&) { return true; });
    Check(none.found == 0 && none.bonus == 0 && none.total == static_cast<int>(entries.size()) - 1, "nothing found: 0 / every exhibit but the secret");
    Check(every.found == every.total && every.bonus == 1 && pt::game::ArchivePercent(every) == 100, "everything found: 100%, one bonus");
    const auto gameplus = pt::game::ArchiveCount(entries, static_cast<int>(ArchiveSection::GamePlus), [](const pt::game::ArchiveEntry&) { return false; });
    Check(gameplus.total == 0 && gameplus.bonus == 0, "the Game+ extras hall counts nothing until its secret is found");
    const auto almost = pt::game::ArchiveCount(entries, -1, [](const pt::game::ArchiveEntry& e) { return e.id != "sub05"; });
    Check(almost.found == almost.total - 1 && pt::game::ArchivePercent(almost) == 99, "one exhibit missing: 99%, never rounded up to 100");
    int halls_total = 0;
    for (int s = 0; s < pt::game::kArchiveSectionCount; ++s) {
        const auto hall = pt::game::ArchiveCount(entries, s, [](const pt::game::ArchiveEntry&) { return true; });
        halls_total += hall.total;
        Check(hall.total > 0 || s == static_cast<int>(ArchiveSection::GamePlus), "hall " + std::to_string(s) + " has exhibits");
    }
    Check(halls_total == every.total, "the halls' totals add up to the museum's");

    const std::set<std::string> expected_finish = {
        "demo:gc_p02_500", "demo:gc_p00_022", "demo:gc_p02_510", "demo:gc_p00_020", "demo:gc_p00_160", "demo:gc_p00_010", "demo:gc_p05_010",
        "demo:gc_p04_310", "demo:gc_p01_070", "demo:gc_p04_340", "demo:gc_p01_081", "demo:gc_p01_011", "demo:gc_p01_020", "demo:gc_p03_010",
        "demo:gc_p04_300", "demo:gc_p04_350", "demo:gc_p03_070", "demo:gc_p03_011", "demo:gc_p01_110", "demo:gc_p04_320", "demo:gc_p04_120",
        "demo:gc_p04_280", "demo:gc_p02_520", "demo:gc_p02_090", "demo:gc_p02_080", "demo:gc_p06_010_final", "gimmick:Bag", "gimmick:Baby",
        "gimmick:CeilLamp", "gimmick:Ocho", "gimmick:Freezer", "floor:f010", "floor:f005", "floor:f050", "floor:f160", "voice:tria1000_101010",
        "voice:tria1000_1n1010", "voice:tria1000_1b1010", "event:Play_radio_religion_01", "overlay", "event:Play_Peephole_Theater",
        "voice:tria1000_1g1010", "voice:tria1000_171010", "event:Play_bg_startroom01", "event:Play_bg_passage01", "event:Play_bgm_unrest03",
        "event:Play_bgm_unrest02", "event:Play_bgm_corridor_f110",
        "demo:gc_p00_030", "demo:gc_p01_050", "demo:gc_p01_090", "demo:gc_p01_022", "event:Play_bgm_herald01"};
    std::set<std::string> finish_keys;
    for (const std::string_view key : pt::game::ArchiveFinishKeys()) {
        Check(finish_keys.insert(std::string(key)).second, std::string(key) + " is listed once");
        Check(pt::game::IsArchiveUnlockKey(key), std::string(key) + " (retroactive) is an unlock key");
    }
    Check(finish_keys == expected_finish, "the retroactive unlock grants exactly the guaranteed keys");
    for (const char* never : {"sub:1", "sub:4", "sub:5", "sub:10", "noise", "bug:sh_bug_1", "bug:sh_bug_5", "bug:sh_bug_3", "voice:tria1000_1d1010",
                              "voice:tria1000_1e1010", "voice:tria1000_1f1010", "voice:tria1000_111010", "voice:tria1000_121010", "demo:gc_p07_030",
                              "demo:gc_p01_021", "demo:gc_p01_010", "photo:PhotoLisa", "photo:PhotoOption", "photo:complete", "lisa_date",
                              "finished"}) {
        Check(!finish_keys.contains(never), std::string(never) + " is not granted retroactively");
    }
    for (const pt::game::ArchiveEntry& e : entries) {
        if (!finish_keys.contains(pt::game::ArchiveUnlockKey(e))) continue;
        for (const char* conditional : {"random", "zoom", "radio_wait", "caught", "bag", "photo"}) {
            Check(e.hint.find(conditional) == std::string_view::npos, std::string(e.id) + " (retroactive) has no conditional hint " + conditional);
        }
    }

    const std::map<std::string, std::string> heard = {
        {"music_startroom", "event:Play_bg_startroom01"}, {"music_hallway", "event:Play_bg_passage01"},
        {"music_unrest03", "event:Play_bgm_unrest03"}, {"music_herald", "event:Play_bgm_herald01"},
        {"music_unrest02", "event:Play_bgm_unrest02"}, {"music_f110", "event:Play_bgm_corridor_f110"}, {"music_ending", "demo:gc_p06_010_final"}};
    int tracks = 0;
    for (const pt::game::ArchiveEntry& e : pt::game::ArchiveEntries()) {
        if (e.media != pt::game::ArchiveMedia::Music) continue;
        ++tracks;
        const std::string id(e.id);
        Check(e.section == ArchiveSection::Music, id + " is in the music hall");
        Check(e.asset.starts_with("media:") || e.asset.starts_with("stream:"), id + " plays a media or a stream of the game");
        const std::string key = pt::game::ArchiveUnlockKey(e);
        if (const auto it = heard.find(id); it != heard.end()) {
            Check(key == it->second, id + " opens when " + it->second + " is reached in play");
            Check(e.note == "pc_note_archive_music", id + "'s note");
        } else {
            Check(key == "finished" && e.note == "pc_note_archive_music_unused", id + " is unused: it opens with the finish");
        }
    }
    Check(tracks == 10, "ten tracks in the music hall");
    for (const auto& [id, key] : heard) Check(Opened(key).contains(id), key + " opens " + id);
    Check(pt::audio::Fnv1Hash32("Play_bgm_herald01") == 0xD567DD28u, "Play_bgm_herald01 is the id OchoLogic posts");
    Check(Entry("music_f110").seconds == 292.5f && Entry("music_f110").loop_from == 42.5f, "the f110 music's pass and loop point");
    Check(pt::game::ArchiveSectionCover(ArchiveSection::Music) == "music_f110", "the music hall's cover");

    const std::map<std::string, std::string> lisa = {{"lisa_bathroom_walk", "demo:gc_p01_090"}, {"lisa_backwards", "demo:gc_p04_120"},
                                                     {"lisa_catch", "demo:gc_p07_030"}};
    for (const auto& [id, key] : lisa) {
        const pt::game::ArchiveEntry& e = Entry(id);
        Check(e.media == pt::game::ArchiveMedia::Demo && e.free_camera && e.section == ArchiveSection::Models, id + " is a free camera demo in the models hall");
        Check(pt::game::ArchiveUnlockKey(e) == key && Opened(key).size() == 2, id + " opens with " + key + " and its cutscene");
    }
    Check(Entry("lisa_catch").extra == "kill" && Entry("lisa_catch").floor == Entry("kill").floor, "the catch stands as the kill cutscene does");

    for (const char* key : {"pc_archive_music", "pc_note_archive_music_section", "pc_note_archive_music", "pc_note_archive_music_unused",
                            "pc_archive_music_play_hint", "pc_archive_music_pause_hint", "pc_archive_music_startroom", "pc_archive_music_hallway",
                            "pc_archive_music_unrest03", "pc_archive_music_herald", "pc_archive_music_unrest02", "pc_archive_music_f110",
                            "pc_archive_music_ending", "pc_archive_music_unused", "pc_archive_lisa_bathroom_walk", "pc_archive_lisa_backwards",
                            "pc_archive_lisa_catch", "pc_note_archive_free", "pc_archive_free_hint"}) {
        Check(pt::game::PcText(key, 0) != key, std::string(key) + " has its English text");
        for (int language = 1; language < 14; ++language) {
            const std::string_view text = pt::game::PcText(key, language);
            Check(text != key && text != pt::game::PcText(key, 0), std::string(key) + " is translated in language " + std::to_string(language));
        }
    }
    return failed ? 1 : 0;
}
