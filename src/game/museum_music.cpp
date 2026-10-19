#include "game/museum_music.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "engine/audio/sound_package.h"
#include "engine/audio/wem.h"
#include "engine/core/log.h"
#include "engine/fs/vfs.h"
#include "game/archive.h"
#include "game/game_sound.h"

namespace pt::game {
namespace {

constexpr float kStopFade = 0.05f;

class TrackReader final : public audio::MediaReader {
public:
    TrackReader(std::unique_ptr<audio::MediaReader> inner, uint32_t channels, uint64_t base, uint64_t end, uint64_t loop_from,
                std::shared_ptr<MuseumMusic::State> state)
        : inner_(std::move(inner)), channels_(channels), base_(base), end_(end), loop_from_(loop_from), pos_(base), state_(std::move(state)) {
        Jump(base_);
    }

    uint32_t Read(float* out, uint32_t frames) override {
        uint32_t n = 0;
        int empty = 0;
        while (n < frames) {
            if (pos_ >= end_) {
                if (!state_->loop.load(std::memory_order_relaxed) || loop_from_ >= end_ || !Jump(loop_from_)) {
                    state_->ended.store(true, std::memory_order_relaxed);
                    break;
                }
            }
            const uint32_t want = static_cast<uint32_t>(std::min<uint64_t>(frames - n, end_ - pos_));
            const uint32_t got = inner_->Read(out + static_cast<size_t>(n) * channels_, want);
            if (got == 0) {
                end_ = pos_;
                if (++empty > 1) break;
                continue;
            }
            pos_ += got;
            n += got;
        }
        state_->position.store(pos_, std::memory_order_relaxed);
        return n;
    }

    bool Seek(uint64_t frame) override {
        uint64_t target = base_ + frame;
        if (target >= end_ && state_->loop.load(std::memory_order_relaxed) && loop_from_ < end_) {
            target = loop_from_ + (target - loop_from_) % (end_ - loop_from_);
        }
        return Jump(std::min(target, end_));
    }

    uint64_t Position() const override { return pos_ - std::min(pos_, base_); }

private:
    bool Jump(uint64_t frame) {
        if (!inner_->Seek(frame)) return false;
        pos_ = frame;
        state_->position.store(pos_, std::memory_order_relaxed);
        return true;
    }

    std::unique_ptr<audio::MediaReader> inner_;
    uint32_t channels_ = 1;
    uint64_t base_ = 0;
    uint64_t end_ = 0;
    uint64_t loop_from_ = 0;
    uint64_t pos_ = 0;
    std::shared_ptr<MuseumMusic::State> state_;
};

}

std::shared_ptr<const audio::Media> MuseumMusic::LoadSource(Vfs& vfs, GameSound& sound, std::string_view asset) {
    if (asset.starts_with("media:")) {
        uint32_t id = 0;
        const std::string_view digits = asset.substr(6);
        if (std::from_chars(digits.data(), digits.data() + digits.size(), id).ec != std::errc()) return nullptr;
        return sound.System().FindMedia(id);
    }
    if (asset.starts_with("stream:")) {
        std::string path(asset.substr(7));
        std::optional<std::vector<uint8_t>> bytes;
        if (const size_t slash = path.find_last_of('/'); slash != std::string::npos) {
            bytes = vfs.ReadFile(path.substr(0, slash + 1) + "#Eng/" + path.substr(slash + 1));
        }
        if (!bytes) bytes = vfs.ReadFile(path);
        if (!bytes) return nullptr;
        auto stream = audio::ExtractDemoStreamAudio(*bytes);
        if (!stream || stream->wem.empty()) return nullptr;
        auto storage = std::make_shared<const std::vector<uint8_t>>(std::move(stream->wem));
        std::string error;
        auto media = audio::Media::Create(0, storage, 0, storage->size(), &error);
        if (!media) LogWarn("museum: stream {} unreadable: {}", path, error);
        return media;
    }
    return nullptr;
}

bool MuseumMusic::Play(Vfs& vfs, GameSound& sound, const ArchiveEntry& entry, double from) {
    Stop(&sound);
    if (!sound.Ready()) return false;
    source_ = LoadSource(vfs, sound, entry.asset);
    if (!source_ || source_->Frames() == 0 || source_->SampleRate() == 0) {
        LogWarn("museum: no media for {} ({})", entry.id, entry.asset);
        source_.reset();
        return false;
    }
    entry_ = &entry;
    rate_ = source_->SampleRate();
    end_ = source_->Frames();
    if (entry.seconds > 0.0f) end_ = std::min<uint64_t>(end_, static_cast<uint64_t>(std::llround(entry.seconds * rate_)));
    loop_from_ = std::min<uint64_t>(static_cast<uint64_t>(std::llround(entry.loop_from * rate_)), end_);
    state_ = std::make_shared<State>();
    state_->loop.store(loop_);
    const uint64_t frame = std::min<uint64_t>(static_cast<uint64_t>(std::max(from, 0.0) * rate_), end_ > 0 ? end_ - 1 : 0);
    if (!Start(sound, frame)) {
        Stop(&sound);
        return false;
    }
    LogInfo("museum: {} plays {} ({:.1f} s, from {:.1f} s, playing id {})", entry.id, entry.asset, Length(), from, playing_);
    return true;
}

bool MuseumMusic::Start(GameSound& sound, uint64_t frame) {
    if (!source_ || !state_) return false;
    state_->ended.store(false);
    state_->position.store(frame);
    last_position_ = static_cast<double>(frame) / rate_;
    const uint32_t channels = source_->Channels();
    const auto source = source_;
    const auto state = state_;
    const uint64_t end = end_;
    const uint64_t loop_from = loop_from_;
    auto media = audio::Media::CreateGenerated(channels, rate_, uint64_t{1} << 40, [source, state, channels, frame, end, loop_from]() {
        return std::make_unique<TrackReader>(source->OpenReader(), channels, frame, end, loop_from, state);
    }, source_->Info().channel_mask);
    playing_ = media ? sound.PlayMedia(std::move(media), nullptr) : 0;
    paused_ = false;
    return playing_ != 0;
}

void MuseumMusic::Pause(GameSound& sound) {
    if (!entry_ || paused_) return;
    held_ = state_ ? state_->position.load() : 0;
    if (playing_) sound.StopPlayingId(playing_, kStopFade);
    playing_ = 0;
    paused_ = true;
    LogInfo("museum: {} paused at {:.1f} s", entry_->id, Position());
}

bool MuseumMusic::Resume(GameSound& sound) {
    if (!entry_ || !paused_) return false;
    const bool started = Start(sound, std::min(held_, end_ > 0 ? end_ - 1 : 0));
    LogInfo("museum: {} resumes at {:.1f} s", entry_->id, Position());
    return started;
}

void MuseumMusic::Seek(GameSound& sound, double seconds) {
    if (!entry_) return;
    const uint64_t frame = std::min<uint64_t>(static_cast<uint64_t>(std::clamp(seconds, 0.0, Length()) * rate_), end_ > 0 ? end_ - 1 : 0);
    if (paused_) {
        held_ = frame;
        if (state_) state_->position.store(frame);
    } else {
        if (playing_) sound.StopPlayingId(playing_, kStopFade);
        playing_ = 0;
        Start(sound, frame);
    }
    LogInfo("museum: {} at {:.1f} s", entry_->id, static_cast<double>(frame) / rate_);
}

void MuseumMusic::Stop(GameSound* sound) {
    if (sound && playing_) sound->StopPlayingId(playing_, kStopFade);
    if (entry_) LogInfo("museum: {} stops", entry_->id);
    playing_ = 0;
    paused_ = false;
    entry_ = nullptr;
    source_.reset();
    state_.reset();
    held_ = 0;
}

void MuseumMusic::SetLoop(bool on) {
    loop_ = on;
    if (state_) state_->loop.store(on);
    LogInfo("museum: loop {}", on ? "on" : "off");
}

double MuseumMusic::Position() const {
    if (!entry_ || !state_ || rate_ == 0) return 0.0;
    const uint64_t frame = paused_ ? held_ : state_->position.load(std::memory_order_relaxed);
    return static_cast<double>(std::min(frame, end_)) / rate_;
}

double MuseumMusic::Length() const {
    return entry_ && rate_ ? static_cast<double>(end_) / rate_ : 0.0;
}

bool MuseumMusic::Update(GameSound& sound) {
    if (!entry_ || paused_ || !playing_) return false;
    if (sound.IsPlaying(playing_)) {
        const double now = Position();
        if (loop_ && now + 1.0 < last_position_) LogInfo("museum: {} looped back to {:.1f} s", entry_->id, now);
        last_position_ = now;
        return false;
    }
    LogInfo("museum: {} ended", entry_->id);
    playing_ = 0;
    paused_ = true;
    held_ = 0;
    if (state_) state_->position.store(0);
    return true;
}

}
