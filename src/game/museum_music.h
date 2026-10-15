#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

namespace pt {
class Vfs;
}

namespace pt::audio {
class Media;
}

namespace pt::game {

class GameSound;
struct ArchiveEntry;

class MuseumMusic {
public:
    static std::shared_ptr<const audio::Media> LoadSource(Vfs& vfs, GameSound& sound, std::string_view asset);

    bool Play(Vfs& vfs, GameSound& sound, const ArchiveEntry& entry, double from = 0.0);
    void Pause(GameSound& sound);
    bool Resume(GameSound& sound);
    void Seek(GameSound& sound, double seconds);
    void Stop(GameSound* sound);
    void SetLoop(bool on);
    bool Loop() const { return loop_; }

    const ArchiveEntry* Entry() const { return entry_; }
    bool Playing() const { return entry_ && playing_ != 0 && !paused_; }
    bool Paused() const { return entry_ && paused_; }
    double Position() const;
    double Length() const;
    bool Update(GameSound& sound);

    struct State {
        std::atomic<uint64_t> position{0};
        std::atomic<bool> loop{false};
        std::atomic<bool> ended{false};
    };

private:
    bool Start(GameSound& sound, uint64_t frame);

    const ArchiveEntry* entry_ = nullptr;
    std::shared_ptr<const audio::Media> source_;
    std::shared_ptr<State> state_;
    uint32_t playing_ = 0;
    bool paused_ = false;
    bool loop_ = false;
    uint32_t rate_ = 48000;
    uint64_t end_ = 0;
    uint64_t loop_from_ = 0;
    uint64_t held_ = 0;
    double last_position_ = 0.0;
};

}
