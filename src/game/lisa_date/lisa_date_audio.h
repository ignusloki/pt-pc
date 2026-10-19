#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pt::audio {
class Media;
}

namespace pt::game {

struct LisaDatePcm {
    uint32_t channels = 0;
    uint32_t rate = 0;
    std::vector<int16_t> samples;
};

bool LisaDateDecodeOgg(std::span<const uint8_t> ogg, LisaDatePcm& pcm);
bool LisaDateDecodeMusic(const std::filesystem::path& dir, const std::string& name, LisaDatePcm& pcm, float max_seconds = 0.0f);
LisaDatePcm LisaDateSlowed(const LisaDatePcm& pcm, float speed);
std::vector<uint8_t> LisaDateWav(const LisaDatePcm& pcm);

inline constexpr double kLisaMusicLoopA = 33.553;
inline constexpr double kLisaMusicLoopB = 86.583;
inline constexpr double kLisaMusicCrossfade = 1.5;

struct LisaMusicLoop {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t crossfade = 0;
    bool Valid() const { return b > a && crossfade > 0 && crossfade <= a && crossfade <= b - a; }
};
LisaMusicLoop LisaMusicLoopFor(const LisaDatePcm& pcm, double speed = 1.0);
void LisaMusicCrossfadeGains(uint64_t k, uint64_t n, float& out_gain, float& in_gain);

struct LisaMusicState {
    std::atomic<bool> release{false};
    std::atomic<uint32_t> seams{0};
    std::atomic<bool> outro{false};
    std::atomic<bool> ended{false};
    std::atomic<uint64_t> position{0};
};

class LisaMusicPlayer {
public:
    LisaMusicPlayer(std::shared_ptr<const LisaDatePcm> pcm, LisaMusicLoop loop, std::shared_ptr<LisaMusicState> state);
    uint32_t Render(float* out, uint32_t frames);
    void Seek(uint64_t played);
    uint64_t Played() const { return played_; }
    uint64_t FilePosition() const { return pos_; }
    static uint64_t FramesToEnd(const LisaMusicLoop& loop, uint64_t total, uint64_t pos, bool crossing);

private:
    std::shared_ptr<const LisaDatePcm> pcm_;
    LisaMusicLoop loop_;
    std::shared_ptr<LisaMusicState> state_;
    uint64_t total_ = 0;
    uint64_t pos_ = 0;
    uint64_t played_ = 0;
    bool crossing_ = false;
};

std::shared_ptr<const audio::Media> LisaMusicMedia(std::shared_ptr<const LisaDatePcm> pcm, LisaMusicLoop loop,
                                                   std::shared_ptr<LisaMusicState> state);

LisaDatePcm LisaDateExcerpt(const LisaDatePcm& music, float seconds, float gain, float fade);
LisaDatePcm LisaDateHeartbeat(float gain);
LisaDatePcm LisaDateSadNote(float gain);
LisaDatePcm LisaDateShutter(float gain);

}
