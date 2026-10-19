#ifdef NDEBUG
#undef NDEBUG
#endif
#include "game/lisa_date/lisa_date_audio.h"
#include "game/lisa_date/lisa_date_hints.h"
#include "game/lisa_date/lisa_date_script.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using pt::game::LisaDateCommand;
using pt::game::LisaDateRun;
using pt::game::LisaDateScript;

namespace {

using pt::game::LisaMusicLoop;
using pt::game::LisaMusicPlayer;
using pt::game::LisaMusicState;

std::vector<float> RenderMusic(LisaMusicPlayer& player, uint32_t channels, uint64_t frames, uint64_t mark = ~uint64_t{0},
                               const std::function<void()>& at = {}) {
    std::vector<float> out;
    std::vector<float> block(2048 * channels);
    uint64_t done = 0;
    uint32_t size = 1;
    while (done < frames) {
        if (at && done == mark) at();
        uint32_t want = static_cast<uint32_t>(std::min<uint64_t>(frames - done, size));
        if (done < mark) want = static_cast<uint32_t>(std::min<uint64_t>(want, mark - done));
        const uint32_t got = player.Render(block.data(), want);
        out.insert(out.end(), block.begin(), block.begin() + static_cast<size_t>(got) * channels);
        done += got;
        if (got < want) break;
        size = size % 1000 + 37;
    }
    return out;
}

void MusicLoopTests() {
    for (uint64_t n : {1ull, 2ull, 7ull, 72000ull}) {
        float last_out = 2.0f;
        for (uint64_t k = 0; k < n; ++k) {
            float g_out = 0.0f, g_in = 0.0f;
            pt::game::LisaMusicCrossfadeGains(k, n, g_out, g_in);
            assert(std::abs(g_out * g_out + g_in * g_in - 1.0f) < 1e-5f);
            assert(g_out <= last_out && g_out >= -1e-6f && g_in <= 1.0f);
            last_out = g_out;
        }
        float g_out = 1.0f, g_in = 0.0f;
        pt::game::LisaMusicCrossfadeGains(n - 1, n, g_out, g_in);
        assert(std::abs(g_out) < 1e-6f && g_in == 1.0f);
    }
    {
        float g_out = 0.0f, g_in = 0.0f;
        pt::game::LisaMusicCrossfadeGains(35999, 72000, g_out, g_in);
        assert(std::abs(g_out - g_in) < 1e-4f && std::abs(g_out - 0.70710678f) < 1e-4f);
    }

    pt::game::LisaDatePcm long_pcm;
    long_pcm.channels = 2;
    long_pcm.rate = 48000;
    long_pcm.samples.assign(2 * 6510643ull, 0);
    const LisaMusicLoop real = pt::game::LisaMusicLoopFor(long_pcm);
    assert(real.Valid() && real.a == 1610544 && real.b == 4155984 && real.crossfade == 72000);
    const LisaMusicLoop slow = pt::game::LisaMusicLoopFor(long_pcm, 0.82);
    assert(slow.Valid() && slow.a == 1964078 && slow.b == 5068273 && slow.crossfade == 87805);
    long_pcm.samples.resize(2 * 4155983ull);
    assert(!pt::game::LisaMusicLoopFor(long_pcm).Valid());

    auto pcm = std::make_shared<pt::game::LisaDatePcm>();
    pcm->channels = 2;
    pcm->rate = 1000;
    for (int f = 0; f < 10000; ++f) {
        pcm->samples.push_back(static_cast<int16_t>(f));
        pcm->samples.push_back(static_cast<int16_t>(-f));
    }
    const LisaMusicLoop loop{3000, 7000, 500};
    auto value = [](float v) { return v * 32768.0f; };
    {
        auto state = std::make_shared<LisaMusicState>();
        LisaMusicPlayer player(pcm, loop, state);
        const std::vector<float> out = RenderMusic(player, 2, 7000 + 4000 * 2 + 10);
        assert(out.size() == 2 * (7000 + 4000 * 2 + 10));
        for (uint64_t i = 0; i < 6500; ++i) assert(value(out[i * 2]) == static_cast<float>(i) && value(out[i * 2 + 1]) == -static_cast<float>(i));
        for (uint64_t pass = 0; pass < 3; ++pass) {
            const uint64_t seam = 7000 + pass * 4000;
            for (uint64_t k = 0; k < 500; ++k) {
                float g_out = 0.0f, g_in = 0.0f;
                pt::game::LisaMusicCrossfadeGains(k, 500, g_out, g_in);
                const float want = (6500.0f + k) * g_out + (2500.0f + k) * g_in;
                assert(std::abs(value(out[(seam - 500 + k) * 2]) - want) < 0.01f);
            }
            if (seam + 1 < out.size() / 2) {
                assert(std::abs(value(out[(seam - 1) * 2]) - 2999.0f) < 0.01f);
                assert(value(out[seam * 2]) == 3000.0f && value(out[(seam + 1) * 2]) == 3001.0f);
            }
        }
        assert(state->seams.load() == 3 && !state->outro.load() && !state->ended.load());
        assert(player.FilePosition() == 3010 && state->position.load() == 3010);
    }
    {
        auto state = std::make_shared<LisaMusicState>();
        LisaMusicPlayer player(pcm, loop, state);
        uint64_t predicted = 0;
        const std::vector<float> out = RenderMusic(player, 2, 1u << 20, 8000, [&] {
            state->release.store(true);
            predicted = 8000 + LisaMusicPlayer::FramesToEnd(loop, 10000, state->position.load(), false);
        });
        const uint64_t frames = out.size() / 2;
        assert(frames == 7000 + (10000 - 3000) && frames == predicted);
        for (uint64_t i = 7000; i < frames; ++i) assert(value(out[i * 2]) == static_cast<float>(3000 + (i - 7000)));
        assert(state->seams.load() == 1 && state->outro.load() && state->ended.load());
        assert(player.Render(nullptr, 64) == 0);
    }
    {
        auto state = std::make_shared<LisaMusicState>();
        LisaMusicPlayer player(pcm, loop, state);
        uint64_t predicted = 0;
        const std::vector<float> out = RenderMusic(player, 2, 1u << 20, 6700, [&] {
            state->release.store(true);
            predicted = 6700 + LisaMusicPlayer::FramesToEnd(loop, 10000, state->position.load(), true);
        });
        const uint64_t frames = out.size() / 2;
        assert(frames == 7000 + 7000 && frames == predicted);
        assert(state->seams.load() == 1 && state->outro.load() && state->ended.load());
        assert(std::abs(value(out[6999 * 2]) - 2999.0f) < 0.01f && value(out[7000 * 2]) == 3000.0f);
        for (uint64_t i = 10500; i < frames; ++i) assert(value(out[i * 2]) == static_cast<float>(3000 + (i - 7000)));
    }
    {
        auto state = std::make_shared<LisaMusicState>();
        state->release.store(true);
        LisaMusicPlayer player(pcm, loop, state);
        assert(RenderMusic(player, 2, 1u << 20).size() == 2 * 10000 && state->seams.load() == 0 && state->ended.load());
        LisaMusicPlayer again(pcm, loop, std::make_shared<LisaMusicState>());
        again.Seek(7000 + 4000 + 12);
        assert(again.FilePosition() == 3012);
        again.Seek(6900);
        std::vector<float> two(4);
        assert(again.Render(two.data(), 2) == 2 && std::abs(value(two[0]) - (6900.0f * std::cos(401.0f / 500.0f * 1.5707963f) +
                                                                             2900.0f * std::sin(401.0f / 500.0f * 1.5707963f))) < 0.05f);
        LisaMusicPlayer once(pcm, LisaMusicLoop{}, std::make_shared<LisaMusicState>());
        assert(RenderMusic(once, 2, 1u << 20).size() == 2 * 10000);
    }

    pt::game::LisaDatePcm decoded;
    if (!pt::game::LisaDateDecodeMusic(std::filesystem::path(PT_LISA_DATE_DIR), "music", decoded)) {
        std::printf("lisa date music: no music.ogg in %s, its seam check skipped\n", PT_LISA_DATE_DIR);
        return;
    }
    auto music = std::make_shared<const pt::game::LisaDatePcm>(std::move(decoded));
    const LisaMusicLoop points = pt::game::LisaMusicLoopFor(*music);
    assert(music->rate == 48000 && music->channels == 2 && points.Valid());
    const uint64_t total = music->samples.size() / 2;
    assert(total > 135.6 * 48000 && total < 135.7 * 48000);
    auto source_step = [&](uint64_t from, uint64_t to) {
        float step = 0.0f;
        for (uint64_t f = from + 1; f < to; ++f) {
            for (uint32_t c = 0; c < 2; ++c) {
                step = std::max(step, std::abs(static_cast<float>(music->samples[f * 2 + c] - music->samples[(f - 1) * 2 + c])) / 32768.0f);
            }
        }
        return step;
    };
    const float around = std::max(source_step(points.a - 96000, points.a + 96000), source_step(points.b - 96000, points.b + 96000));
    auto state = std::make_shared<LisaMusicState>();
    LisaMusicPlayer player(music, points, state);
    const uint64_t from = points.b - points.crossfade - 24000;
    player.Seek(from);
    const std::vector<float> out = RenderMusic(player, 2, points.crossfade + 48000);
    assert(state->seams.load() == 1);
    float worst = 0.0f;
    for (uint64_t f = 1; f < out.size() / 2; ++f) {
        for (uint32_t c = 0; c < 2; ++c) worst = std::max(worst, std::abs(out[f * 2 + c] - out[(f - 1) * 2 + c]));
    }
    const uint64_t seam = points.b - from;
    float seam_step = 0.0f, file_step = 0.0f;
    for (uint32_t c = 0; c < 2; ++c) {
        assert(std::abs(out[(seam - 1) * 2 + c] - music->samples[(points.a - 1) * 2 + c] / 32768.0f) < 1e-4f);
        assert(out[seam * 2 + c] == music->samples[points.a * 2 + c] / 32768.0f);
        seam_step = std::max(seam_step, std::abs(out[seam * 2 + c] - out[(seam - 1) * 2 + c]));
        file_step = std::max(file_step, std::abs(static_cast<float>(music->samples[points.a * 2 + c] - music->samples[(points.a - 1) * 2 + c])) / 32768.0f);
    }
    std::printf("lisa date music: %.3f s, loop %.3f..%.3f s, seam step %.5f (the file's own at A %.5f), largest step %.5f across the "
                "crossfade, %.5f in the music around A and B\n",
                total / 48000.0, points.a / 48000.0, points.b / 48000.0, seam_step, file_step, worst, around);
    assert(std::abs(seam_step - file_step) < 1e-4f && worst <= around * 1.05f);
}

std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string Play(const LisaDateScript& script, const std::vector<int>& picks, int* affection = nullptr) {
    LisaDateRun run(script);
    run.Restart();
    size_t next = 0;
    for (int guard = 0; guard < 1000; ++guard) {
        std::vector<LisaDateRun::Directive> directives;
        const LisaDateRun::Stop stop = run.Advance(directives);
        if (stop == LisaDateRun::Stop::Choice) {
            const int pick = next < picks.size() ? picks[next++] : 1;
            assert(run.Choose(pick - 1));
        } else if (stop == LisaDateRun::Stop::Ending) {
            if (affection) *affection = run.Affection();
            return std::string(run.EndingName());
        } else if (stop == LisaDateRun::Stop::Done) {
            return "done";
        }
    }
    return "loop";
}

std::string SelfieReaction(const LisaDateScript& script, int lisa, int you) {
    LisaDateRun run(script);
    run.Restart();
    bool taken = false;
    for (int guard = 0; guard < 1000; ++guard) {
        std::vector<LisaDateRun::Directive> directives;
        const LisaDateRun::Stop stop = run.Advance(directives);
        if (stop == LisaDateRun::Stop::Choice) {
            assert(run.Choose(0));
        } else if (stop == LisaDateRun::Stop::Selfie) {
            run.SetVar("selfie:lisa", lisa);
            run.SetVar("selfie:you", you);
            taken = true;
        } else if (stop == LisaDateRun::Stop::Line && taken) {
            return run.Current()->b;
        } else if (stop == LisaDateRun::Stop::Ending || stop == LisaDateRun::Stop::Done) {
            return "none";
        }
    }
    return "loop";
}

}

int main() {
    const std::filesystem::path dir = PT_LISA_DATE_DIR;
    LisaDateScript script;
    std::string error;
    const bool parsed = script.Parse(Read(dir / "script.txt"), &error);
    if (!parsed) std::printf("%s\n", error.c_str());
    assert(parsed);
    assert(script.Choices().size() == 12);
    for (const LisaDateCommand& c : script.Commands()) {
        if (c.op != LisaDateCommand::Op::Say || c.a != "lisa") continue;
        const bool known = c.expr == "neutral" || c.expr == "happy" || c.expr == "shy" || c.expr == "sad" || c.expr == "angry" || c.expr == "creepy";
        if (!known) std::printf("line %s: expression '%s'\n", c.b.c_str(), c.expr.c_str());
        assert(known);
    }

    const auto eng = LisaDateScript::ParseText(Read(dir / "text" / "eng.txt"));
    const auto tur = LisaDateScript::ParseText(Read(dir / "text" / "tur.txt"));
    std::vector<std::string> keys = {"name_lisa", "name_you", "name_glitch", "ui_auto", "ui_skip", "ending_good", "ending_bad", "ending_true"};
    for (const LisaDateCommand& c : script.Commands()) {
        if (c.op == LisaDateCommand::Op::Say || c.op == LisaDateCommand::Op::Scene) keys.push_back(c.b);
    }
    for (const auto& choice : script.Choices()) {
        for (const auto& option : choice.options) keys.push_back(option.key);
    }
    int missing = 0;
    for (const std::string& key : keys) {
        if (!eng.contains(key)) {
            std::printf("eng.txt lacks %s\n", key.c_str());
            ++missing;
        }
        if (!tur.contains(key)) {
            std::printf("tur.txt lacks %s\n", key.c_str());
            ++missing;
        }
    }
    assert(missing == 0);
    for (const auto& [key, text] : tur) assert(eng.contains(key));
    for (const char* code : {"tur", "fra", "deu", "spa", "jpn", "ita", "por", "zhs", "ara", "rus", "ukr", "ces", "pol"}) {
        const auto table = LisaDateScript::ParseText(Read(dir / "text" / (std::string(code) + ".txt")));
        for (const auto& [key, text] : eng) {
            if (!table.contains(key) || table.at(key).empty()) std::printf("%s.txt lacks %s\n", code, key.c_str());
            assert(table.contains(key) && !table.at(key).empty());
        }
        for (const auto& [key, text] : table) assert(eng.contains(key));
    }

    pt::game::LisaDateText text;
    text.SetEnglish(eng);
    text.SetLanguage({{"s1_01", "tr"}});
    assert(text.Get("s1_01") == "tr");
    assert(text.Get("s1_02") == eng.at("s1_02"));
    assert(text.Get("no_such_key") == "no_such_key");

    int affection = 0;
    assert(Play(script, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, &affection) == "good" && affection >= 12);
    assert(Play(script, {2, 3, 3, 3, 2, 3, 3, 2, 2, 2, 2, 3}) == "bad");
    assert(Play(script, {3, 1, 1, 1, 1, 2, 1, 1, 3, 1, 3, 1}) == "true");
    assert(Play(script, {3, 1, 1, 1, 1, 2, 1, 1, 3, 1, 1, 1}) == "true");
    assert(Play(script, {1, 3, 1, 3, 2, 3, 3, 2}) == "bad");
    assert(Play(script, {2, 2, 3, 2, 3, 1, 1, 3, 1, 3, 1, 2}) == "bad");

    assert(SelfieReaction(script, 0, 0) == "good_02");
    assert(SelfieReaction(script, 1, 0) == "good_02" && SelfieReaction(script, 0, -2) == "good_02");
    assert(SelfieReaction(script, 1, 1) == "selfie_both_grin" && SelfieReaction(script, 2, 2) == "selfie_both_grin");
    assert(SelfieReaction(script, -1, -2) == "selfie_both_grumpy" && SelfieReaction(script, -2, -2) == "selfie_both_grumpy");
    assert(SelfieReaction(script, 2, -1) == "selfie_she_smiles");
    assert(SelfieReaction(script, -2, 1) == "selfie_you_smile");

    LisaDateScript bad;
    assert(!bad.Parse("if selfie:cat >= 1 x\nlabel x\n", &error));
    assert(bad.Parse("selfie\nif selfie:you >= 1 x\nlabel x\nending good\n", &error));
    assert(!bad.Parse("choice x\n option a 1\n", &error));
    assert(!bad.Parse("goto nowhere\n", &error));
    assert(!bad.Parse("say cat hello\n", &error));
    assert(bad.Parse("choice x\n option a 1 flag:f\n  say lisa r\n option b -1 goto:z\nend\nsay lisa after\nlabel z\nending good\n", &error));
    assert(Play(bad, {1}) == "good" && Play(bad, {2}) == "good");

    static_assert(pt::game::kLisaRushesNeeded == 3);
    assert(pt::game::LisaHintTintStep(0) == 0.0f);
    assert(pt::game::LisaHintTintStep(1) > 0.0f && pt::game::LisaHintTintStep(1) < 0.2f);
    for (int n = 1; n <= pt::game::kLisaRushesNeeded; ++n) {
        assert(pt::game::LisaHintTintStep(n) > pt::game::LisaHintTintStep(n - 1));
        if (n > 1) assert(pt::game::LisaHintStingSeconds(n) > pt::game::LisaHintStingSeconds(n - 1));
        if (n > 1) assert(pt::game::LisaHintStingGain(n) > pt::game::LisaHintStingGain(n - 1));
    }
    assert(pt::game::LisaHintTintStep(pt::game::kLisaRushesNeeded) > 0.8f);
    assert(pt::game::LisaHintStingSeconds(1) >= 0.9f && pt::game::LisaHintStingSeconds(pt::game::kLisaRushesNeeded) <= 2.5f);
    const pt::game::LisaDatePcm beat = pt::game::LisaDateHeartbeat(0.5f);
    const pt::game::LisaDatePcm sad = pt::game::LisaDateSadNote(0.4f);
    assert(beat.channels == 1 && beat.samples.size() == 24000 && sad.samples.size() > 115000);
    const std::vector<uint8_t> once = pt::game::LisaDateWav(beat);
    assert(once.size() == 44 + beat.samples.size() * 2 && std::memcmp(once.data() + 36, "data", 4) == 0);
    pt::game::LisaDatePcm music;
    music.channels = 2;
    music.rate = 1000;
    music.samples.assign(2 * 5000, 10000);
    const pt::game::LisaDatePcm sting = pt::game::LisaDateExcerpt(music, 2.0f, 0.5f, 0.5f);
    assert(sting.samples.size() == 2 * 2000 && sting.samples[2 * 1000] == 5000 && sting.samples.back() < 100);
    MusicLoopTests();
    std::printf("lisa date script: %zu commands, %zu choices, %zu English keys, ok\n", script.Commands().size(), script.Choices().size(), eng.size());
    return 0;
}
