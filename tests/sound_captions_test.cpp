#include <cstdio>
#include <set>
#include <string>

#include "engine/audio/wwise_bank.h"
#include "engine/core/localized_text.h"
#include "game/sound_captions.h"
#include "game/ui/pc_settings.h"

namespace {

int failures = 0;

void Check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    failures += ok ? 0 : 1;
}

const pt::game::CaptionRule* ByName(const char* name) {
    return pt::game::SoundCaptions::Find(pt::audio::Fnv1Hash32(name));
}

}

int main() {
    using pt::game::CaptionSide;
    using pt::game::SoundCaptions;
    std::set<uint32_t> seen;
    bool hashes = true;
    bool unique = true;
    for (const pt::game::CaptionRule& rule : SoundCaptions::Rules()) {
        if (rule.name) {
            hashes &= pt::audio::Fnv1Hash32(rule.name) == rule.event;
        }
        unique &= seen.insert(rule.event).second;
    }
    Check(hashes, "every named rule's event id is the FNV-1 hash of its name");
    Check(unique, "no event has two rules");

    std::set<std::string> keys = {"pc_cap_left", "pc_cap_right", "pc_cap_behind", "pc_cap_ahead"};
    for (const pt::game::CaptionRule& rule : SoundCaptions::Rules()) {
        keys.insert(rule.key);
    }
    bool english = true;
    int missing = 0;
    for (const std::string& key : keys) {
        english &= pt::game::PcText(key, 0) != key;
        for (int language = 1; language <= 13; ++language) {
            if (pt::localized::Menu(key, language).empty()) {
                std::printf("missing %s in language %d\n", key.c_str(), language);
                ++missing;
            }
        }
    }
    Check(english, "every caption key has an English text");
    Check(missing == 0, "every caption key has a text in every other language");

    const glm::vec3 at(0.0f);
    const glm::vec3 forward(0.0f, 0.0f, 1.0f);
    Check(SoundCaptions::SideOf(at, forward, {0.0f, 0.0f, 5.0f}) == CaptionSide::Ahead, "a source ahead is ahead");
    Check(SoundCaptions::SideOf(at, forward, {0.0f, 0.0f, -5.0f}) == CaptionSide::Behind, "a source behind is behind");
    Check(SoundCaptions::SideOf(at, forward, {-5.0f, 0.0f, 0.0f}) == CaptionSide::Right, "a source on the camera's right side is right");
    Check(SoundCaptions::SideOf(at, forward, {5.0f, 0.0f, 0.0f}) == CaptionSide::Left, "a source on the camera's left side is left");
    Check(SoundCaptions::SideOf(at, forward, {0.3f, 0.0f, -0.4f}) == CaptionSide::None, "a source within a metre has no side");
    Check(SoundCaptions::SideOf(at, glm::vec3(-1.0f, 0.0f, 0.0f), {0.0f, 0.0f, -5.0f}) == CaptionSide::Right, "the sides turn with the listener");
    Check(SoundCaptions::SideOf(at, glm::vec3(0.0f, 1.0f, 0.0f), {5.0f, 0.0f, 0.0f}) == CaptionSide::None, "straight up has no heading, so no side");

    const auto* laugh = SoundCaptions::Find(0xBA6F9BBE);
    const auto* phone = ByName("Play_tel_10_call");
    const auto* door = ByName("Play_sfx_door_open_02");
    const auto* creak = ByName("Play_sfx_house_creak_01");
    const auto* light = ByName("Play_sfx_light_off_01");
    const auto* bell = ByName("Play_sfx_clock_bell");
    Check(laugh && phone && door && creak && light && bell, "the rules the tests use exist");
    if (!(laugh && phone && door && creak && light && bell)) {
        return 1;
    }

    SoundCaptions off;
    off.Post(laugh->event, std::nullopt);
    Check(off.Lines().empty() && off.Pulse(0.016f) == std::pair<uint8_t, uint8_t>(0, 0), "off, a post shows nothing and does not rumble");

    SoundCaptions captions;
    captions.SetEnabled(true);
    captions.Post(0x12345678, std::nullopt);
    Check(captions.Lines().empty(), "an event without a rule shows nothing");
    captions.Post(laugh->event, std::nullopt);
    Check(captions.Lines().size() == 1 && captions.Lines()[0].rule == laugh, "the baby's laugh shows a caption");
    const auto first = captions.Pulse(0.1f);
    Check(first.second > 0, "a caption with a pulse rumbles the small motor");
    captions.Post(laugh->event, std::nullopt);
    Check(captions.Lines().size() == 1, "the same caption renews instead of stacking");
    captions.Pulse(0.3f);
    Check(captions.Pulse(0.016f) == std::pair<uint8_t, uint8_t>(0, 0), "the pulse falls to nothing within its quarter second");
    captions.Post(laugh->event, std::nullopt);
    Check(captions.Pulse(0.0f).second == 0, "a renewed caption does not pulse again");

    captions.Post(phone->event, glm::vec3(0.0f, 0.0f, -6.0f));
    captions.Update(0.1f, at, forward);
    bool behind = false;
    for (const auto& line : captions.Lines()) behind |= line.rule == phone && line.side == CaptionSide::Behind;
    Check(behind, "the phone behind the player says behind");
    captions.Update(0.1f, at, glm::vec3(0.0f, 0.0f, -1.0f));
    bool ahead = false;
    for (const auto& line : captions.Lines()) ahead |= line.rule == phone && line.side == CaptionSide::Ahead;
    Check(ahead, "turning around turns the hint");

    captions.Clear();
    captions.Post(laugh->event, std::nullopt);
    captions.Post(phone->event, glm::vec3(0.0f, 0.0f, 6.0f));
    captions.Post(bell->event, std::nullopt);
    captions.Post(door->event, glm::vec3(0.0f, 0.0f, 3.0f));
    captions.Post(creak->event, std::nullopt);
    bool kept = captions.Lines().size() == SoundCaptions::kMaxLines;
    for (const auto& line : captions.Lines()) kept &= line.rule->priority == 2;
    Check(kept, "the house does not displace a puzzle's caption");
    captions.Post(0xDE941744, std::nullopt);
    Check(captions.Lines().size() == SoundCaptions::kMaxLines && captions.Lines().back().rule->event == 0xDE941744 &&
              captions.Lines().front().rule != laugh,
          "a new puzzle caption displaces the oldest");

    captions.Clear();
    captions.Post(door->event, std::nullopt);
    captions.Post(creak->event, std::nullopt);
    captions.Post(light->event, std::nullopt);
    captions.Post(laugh->event, std::nullopt);
    Check(captions.Lines().size() == SoundCaptions::kMaxLines && captions.Lines().back().rule == laugh, "a puzzle caption displaces the house");
    captions.Update(30.0f, at, forward);
    Check(captions.Lines().empty(), "captions clear after their hold");
    std::printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
