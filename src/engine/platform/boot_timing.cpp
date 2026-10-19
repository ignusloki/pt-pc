#include "engine/platform/boot_timing.h"

#include <chrono>
#include <format>
#include <mutex>

#include "engine/core/log.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pt::boot {
namespace {

using Clock = std::chrono::steady_clock;

struct Origin {
    Clock::time_point at = Clock::now();
    double age = 0.0;
    Origin() {
#ifdef _WIN32
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            FILETIME now{};
            GetSystemTimePreciseAsFileTime(&now);
            const auto ticks = [](const FILETIME& t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
            if (ticks(now) > ticks(created)) age = static_cast<double>(ticks(now) - ticks(created)) * 1e-7;
        }
#endif
    }
};

Origin& Start() {
    static Origin origin;
    return origin;
}

std::mutex g_mutex;
double g_last = -1.0;

}

double SinceProcessStart() {
    const Origin& origin = Start();
    return origin.age + std::chrono::duration<double>(Clock::now() - origin.at).count();
}

std::string PhaseText(std::string_view phase, double seconds, std::string_view note) {
    return note.empty() ? std::format("{} {:.3f} s", phase, seconds) : std::format("{} {:.3f} s ({})", phase, seconds, note);
}

void Phase(std::string_view phase, std::string_view note) {
    const double now = SinceProcessStart();
    double took = 0.0;
    {
        std::lock_guard lock(g_mutex);
        took = now - (g_last < 0.0 ? 0.0 : g_last);
        g_last = now;
    }
    LogInfo("boot: {}", PhaseText(phase, took, note));
}

}
