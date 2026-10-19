#include "game/ui/pc_settings.h"

#include <cstdio>
#include <set>
#include <string>
#include <string_view>

int main() {
    int failed = 0;
    for (int language = 0; language < 14; ++language) {
        const std::string playing = pt::game::PcNoteText("pc_note_archive_playing", language);
        const bool ok = playing.find("{accept}") != std::string::npos;
        std::printf("%s playing prompt language %d\n", ok ? "PASS" : "FAIL", language);
        failed += !ok;
    }
    std::set<std::string_view> seen;
    for (const std::string_view key : pt::game::PcDescriptionKeys()) {
        const bool fresh = seen.insert(key).second;
        if (!fresh) std::printf("FAIL: key %.*s is listed twice\n", static_cast<int>(key.size()), key.data());
        failed += !fresh;
    }
    const bool apart = pt::game::PcText("pc_note_archive_gate", 0) != pt::game::PcText("pc_note_archive_locked", 0);
    std::printf("%s museum gate and locked exhibit notes differ\n", apart ? "PASS" : "FAIL");
    failed += !apart;
    return failed ? 1 : 0;
}
