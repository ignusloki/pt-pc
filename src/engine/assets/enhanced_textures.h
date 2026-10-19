#pragma once
#include "engine/assets/texture_cache.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace pt {
uint64_t EnhancedTextureKey(const FtexTexture& source, uint64_t model, EnhancedKind kind = EnhancedKind::Colour);
std::vector<std::string> EnhancedTextureCandidates(const std::filesystem::path& game);
uint64_t EnhancedModelKey(const std::filesystem::path& runtime);
std::filesystem::path EnhancedCacheFile(const std::filesystem::path& root, std::string_view stem);
bool EnhancedManifestCurrent(const std::filesystem::path& game, const std::filesystem::path& cache, const std::filesystem::path& runtime, uint32_t kinds,
                             uint32_t max_output);

class EnhancedTextureJob {
public:
    enum class State { Idle, Running, Ready, Failed, Cancelled };
    struct Status { State state = State::Idle; uint32_t done = 0; uint32_t total = 0; std::string note; };
    ~EnhancedTextureJob();
    bool Start(const std::filesystem::path& game, const std::filesystem::path& cache,
               const std::filesystem::path& runtime, uint32_t limit = 0, uint32_t max_output = 0, uint32_t kinds = kEnhancedAllKinds);
    void Cancel();
    void SetPaused(bool paused) { paused_ = paused; }
    bool Paused() const { return paused_; }
    Status GetStatus() const;
    void Prioritise(std::vector<std::string> stems);
private:
    bool IsPriority(const std::string& stem) const;
    void Run(std::filesystem::path game, std::filesystem::path cache, std::filesystem::path runtime, uint32_t limit, uint32_t max_output,
             uint32_t kinds);
    void Publish(State state, uint32_t done, uint32_t total, std::string note);
    std::atomic<bool> cancel_{false};
    std::atomic<bool> paused_{false};
    std::thread worker_;
    mutable std::mutex mutex_;
    Status status_;
    mutable std::mutex priority_mutex_;
    std::unordered_set<std::string> priority_;
};
}
