#include "engine/assets/enhanced_textures.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <map>

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) { std::fprintf(stderr, "usage: pt_texture_job_test <game> <runtime> <new-cache-dir> [limit=1, 0=all]\n"); return 2; }
    const uint32_t limit = argc == 5 ? static_cast<uint32_t>(std::stoul(argv[4])) : 1;
    const std::filesystem::path game = argv[1], runtime = argv[2], cache = argv[3];
    if (std::filesystem::exists(cache)) { std::fprintf(stderr, "use a new scratch cache directory\n"); return 2; }
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { std::printf("FAIL: %s\n", name); ++failures; } };
    auto wait = [&](pt::EnhancedTextureJob& job) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limit ? 150 : 1800);
        while (job.GetStatus().state == pt::EnhancedTextureJob::State::Running) {
            if (std::chrono::steady_clock::now() > deadline) { job.Cancel(); check(false, "job deadline"); break; }
            SDL_Delay(50);
        }
        const auto s = job.GetStatus();
        std::printf("job: %d, %u/%u: %s\n", static_cast<int>(s.state), s.done, s.total, s.note.c_str());
        return s;
    };
    pt::EnhancedTextureJob job;
    check(job.Start(game, cache, cache / "missing-runtime", 1), "missing runtime request accepted");
    check(wait(job).state == pt::EnhancedTextureJob::State::Failed, "missing runtime reported");
    const auto started = std::chrono::steady_clock::now();
    check(job.Start(game, cache, runtime, limit), "generation starts");
    auto result = wait(job);
    check(result.state == pt::EnhancedTextureJob::State::Ready && result.done > 0 && result.done == result.total, "textures generated");
    std::printf("generation: %.3f seconds\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    std::map<std::filesystem::path, std::filesystem::file_time_type> times;
    if (std::filesystem::exists(cache)) for (const auto& file : std::filesystem::directory_iterator(cache)) {
        if (file.path().extension() == ".pttex") times[file.path()] = file.last_write_time();
    }
    check(!times.empty(), "cache outputs exist");
    check(job.Start(game, cache, runtime, limit), "cache reuse starts");
    check(wait(job).state == pt::EnhancedTextureJob::State::Ready, "cache reuse ready");
    for (const auto& [file, time] : times) check(std::filesystem::last_write_time(file) == time, "cache file not regenerated");
    check(job.Start(game, cache, runtime, 1), "cancel request starts");
    job.Cancel();
    check(wait(job).state == pt::EnhancedTextureJob::State::Cancelled, "cancel terminates worker");
    const auto active_cache = cache / "cancel-active";
    check(job.Start(game, active_cache, runtime, 0), "active cancellation starts");
    const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!std::filesystem::exists(active_cache / "upscaler.log") &&
           job.GetStatus().state == pt::EnhancedTextureJob::State::Running && std::chrono::steady_clock::now() < cancel_deadline) SDL_Delay(10);
    check(std::filesystem::exists(active_cache / "upscaler.log"), "upscaler reached before cancellation");
    job.Cancel();
    check(wait(job).state == pt::EnhancedTextureJob::State::Cancelled, "active upscaler cancellation completes");
    {
        const auto paused_cache = cache / "paused";
        job.SetPaused(true);
        check(job.Start(game, paused_cache, runtime, 4), "paused generation starts");
        SDL_Delay(4000);
        auto count_files = [&](const std::filesystem::path& dir) {
            size_t n = 0;
            if (std::filesystem::exists(dir)) for (const auto& file : std::filesystem::directory_iterator(dir)) n += file.path().extension() == ".pttex";
            return n;
        };
        check(job.GetStatus().state == pt::EnhancedTextureJob::State::Running && count_files(paused_cache) == 0 &&
              !std::filesystem::exists(paused_cache / "upscaler.log"), "a paused job makes nothing");
        job.SetPaused(false);
        const auto result = wait(job);
        check(result.state == pt::EnhancedTextureJob::State::Ready && result.done == result.total && count_files(paused_cache) >= 1, "goes on after the pause");
        const auto stop_cache = cache / "pause-in-batch";
        check(job.Start(game, stop_cache, runtime, 0, 0, pt::kEnhancedColourKinds), "pause in the batch starts");
        const auto upscaler_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!std::filesystem::exists(stop_cache / "upscaler.log") && std::chrono::steady_clock::now() < upscaler_deadline) SDL_Delay(10);
        while (count_files(stop_cache) < 3 && job.GetStatus().state == pt::EnhancedTextureJob::State::Running &&
               std::chrono::steady_clock::now() < upscaler_deadline + std::chrono::seconds(60)) SDL_Delay(20);
        job.SetPaused(true);
        size_t made = count_files(stop_cache), unchanged = 0;
        for (int i = 0; i < 80 && unchanged < 10; ++i) {
            SDL_Delay(500);
            const size_t now = count_files(stop_cache);
            unchanged = now == made ? unchanged + 1 : 0;
            made = now;
        }
        SDL_Delay(4000);
        std::printf("paused: %zu maps made, %zu after four more seconds, state %d\n", made, count_files(stop_cache), static_cast<int>(job.GetStatus().state));
        check(job.GetStatus().state == pt::EnhancedTextureJob::State::Running && count_files(stop_cache) == made, "nothing is made while paused, the upscaler stopped");
        job.SetPaused(false);
        const size_t before_resume = count_files(stop_cache);
        const auto resume_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (count_files(stop_cache) <= before_resume && job.GetStatus().state == pt::EnhancedTextureJob::State::Running &&
               std::chrono::steady_clock::now() < resume_deadline) SDL_Delay(50);
        check(count_files(stop_cache) > before_resume, "maps are made again after the pause");
        job.Cancel();
        check(wait(job).state == pt::EnhancedTextureJob::State::Cancelled, "cancel after a pause");
    }
    {
        std::string preferred;
        for (const auto& name : pt::EnhancedTextureCandidates(game)) if (name.ends_with("_srm")) preferred = name;
        check(!preferred.empty(), "a material map to prefer");
        const auto priority_cache = cache / "priority";
        job.Prioritise({preferred});
        check(job.Start(game, priority_cache, runtime, 0), "priority generation starts");
        const auto file = pt::EnhancedCacheFile(priority_cache, preferred);
        const auto priority_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (!std::filesystem::exists(file) && job.GetStatus().state == pt::EnhancedTextureJob::State::Running &&
               std::chrono::steady_clock::now() < priority_deadline) SDL_Delay(50);
        const auto status = job.GetStatus();
        std::printf("preferred map made with %u of %u done\n", status.done, status.total);
        check(std::filesystem::exists(file) && status.state == pt::EnhancedTextureJob::State::Running && status.done < status.total / 4,
              "the preferred map is made early");
        job.Cancel();
        check(wait(job).state == pt::EnhancedTextureJob::State::Cancelled, "cancel after the priority run");
    }
    std::printf("texture job: %d failures\n", failures);
    return failures ? 1 : 0;
}
