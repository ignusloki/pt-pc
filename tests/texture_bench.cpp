#include "engine/assets/enhanced_textures.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

int main(int argc, char** argv) {
    if (argc < 4 || argc > 7) { std::fprintf(stderr, "usage: pt_texture_bench <game> <runtime> <new-cache-dir> [limit=0] [kinds=%u] [max-output=0]\n", pt::kEnhancedAllKinds); return 2; }
    const std::filesystem::path game = argv[1], runtime = argv[2], cache = argv[3];
    const uint32_t limit = argc > 4 ? static_cast<uint32_t>(std::stoul(argv[4])) : 0;
    const uint32_t kinds = argc > 5 ? static_cast<uint32_t>(std::stoul(argv[5])) : pt::kEnhancedAllKinds;
    const uint32_t max_output = argc > 6 ? static_cast<uint32_t>(std::stoul(argv[6])) : 0;
    if (std::filesystem::exists(cache)) { std::fprintf(stderr, "use a new scratch cache directory\n"); return 2; }
    pt::EnhancedTextureJob job;
    std::vector<std::string> priority;
    if (const char* file = std::getenv("PT_BENCH_PRIORITY"); file && *file) {
        const auto candidates = pt::EnhancedTextureCandidates(game);
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);)
            if (std::binary_search(candidates.begin(), candidates.end(), line)) priority.push_back(line);
        std::printf("priority list: %zu of the file's stems can be enhanced\n", priority.size());
        if (!std::getenv("PT_BENCH_MEASURE_ONLY")) job.Prioritise(priority);
    }
    const auto started = std::chrono::steady_clock::now();
    auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
    if (!job.Start(game, cache, runtime, limit, max_output, kinds)) return 2;
    std::vector<double> seen(priority.size(), -1.0);
    double next_report = 10.0, first_made = -1.0, priority_made = -1.0;
    uint32_t first_done = 0;
    for (;;) {
        const auto s = job.GetStatus();
        if (first_made < 0.0 && s.total && s.done > first_done) first_made = seconds();
        if (first_made < 0.0 && s.total) first_done = s.done;
        size_t priority_done = 0;
        if (!priority.empty()) {
            const double now = seconds();
            for (size_t i = 0; i < priority.size(); ++i) {
                if (seen[i] < 0.0 && std::filesystem::exists(pt::EnhancedCacheFile(cache, priority[i]))) seen[i] = now;
                priority_done += seen[i] >= 0.0;
            }
            if (priority_made < 0.0 && priority_done * 10 >= priority.size() * 9) priority_made = now;
        }
        if (s.state != pt::EnhancedTextureJob::State::Running) {
            if (!priority.empty()) {
                std::vector<double> times;
                for (const double t : seen) if (t >= 0.0) times.push_back(t);
                std::sort(times.begin(), times.end());
                for (const int percent : {10, 25, 50, 75, 90, 100}) {
                    if (times.empty()) break;
                    const size_t at = std::min(times.size() - 1, times.size() * percent / 100 - (percent == 100 ? 1 : 0));
                    std::printf("%d%% of the %zu priority maps made by %.1f s\n", percent, times.size(), times.empty() ? -1.0 : times[at]);
                }
            }
            std::printf("finished: state %d, %u / %u in %.1f s (first map made after %.1f s, %zu of %zu priority maps after %.1f s): %s\n",
                        static_cast<int>(s.state), s.done, s.total, seconds(), first_made, priority.size() * 9 / 10, priority.size(), priority_made, s.note.c_str());
            return s.state == pt::EnhancedTextureJob::State::Ready ? 0 : 1;
        }
        if (seconds() >= next_report) {
            std::printf("%.0f s: %u / %u (priority maps %zu / %zu)\n", seconds(), s.done, s.total, priority_done, priority.size());
            std::fflush(stdout);
            next_report += 10.0;
        }
        SDL_Delay(100);
    }
}
