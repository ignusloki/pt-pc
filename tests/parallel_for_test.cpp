#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "engine/core/parallel_for.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s: %s\n", what, ok ? "PASS" : "FAIL");
    failures += !ok;
}

double Ms(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
}

bool ChunksOnce(size_t count, size_t chunks) {
    std::vector<std::atomic<int>> seen(count);
    std::vector<std::atomic<int>> ran(chunks);
    std::atomic<bool> ranges_ok{true};
    pt::ParallelChunks(count, chunks, [&](size_t c, size_t begin, size_t end) {
        ran[c].fetch_add(1);
        const size_t n = std::max<size_t>(1, std::min(chunks, count));
        if (begin != count * c / n || end != count * (c + 1) / n) {
            ranges_ok = false;
        }
        for (size_t i = begin; i < end; ++i) {
            seen[i].fetch_add(1);
        }
    });
    bool ok = ranges_ok;
    for (size_t i = 0; i < count; ++i) {
        ok = ok && seen[i] == 1;
    }
    for (size_t c = 0; c < std::min(chunks, count); ++c) {
        ok = ok && ran[c] == 1;
    }
    return ok;
}

void UnderLoaderLock(const char* label) {
#ifdef _WIN32
    using Lock = LONG(NTAPI*)(ULONG, ULONG*, ULONG_PTR*);
    using Unlock = LONG(NTAPI*)(ULONG, ULONG_PTR);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto lock = reinterpret_cast<Lock>(reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrLockLoaderLock")));
    const auto unlock = reinterpret_cast<Unlock>(reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrUnlockLoaderLock")));
    if (!lock || !unlock) {
        std::printf("%s: no LdrLockLoaderLock, skipped\n", label);
        return;
    }
    std::atomic<bool> held{false};
    std::thread holder([&] {
        ULONG state = 0;
        ULONG_PTR cookie = 0;
        lock(0, &state, &cookie);
        held = true;
        Sleep(2000);
        unlock(0, cookie);
    });
    while (!held) {
        std::this_thread::yield();
    }
    const auto start = std::chrono::steady_clock::now();
    std::atomic<size_t> sum{0};
    pt::ParallelChunks(size_t{1} << 20, 8, [&](size_t, size_t begin, size_t end) {
        size_t local = 0;
        for (size_t i = begin; i < end; ++i) {
            local += i & 7;
        }
        sum += local;
    });
    bool task_ran = false;
    pt::ParallelTask task([&] { task_ran = true; });
    task.Wait();
    const double ms = Ms(start);
    holder.join();
    char what[160];
    std::snprintf(what, sizeof(what), "%s: chunks and a task in %.1f ms while the loader lock is held for 2000 ms", label, ms);
    Check(ms < 500.0 && task_ran && sum == (size_t{1} << 20) / 8 * 28, what);
#else
    (void)label;
#endif
}

}

int main() {
    UnderLoaderLock("pool started under the lock");
    UnderLoaderLock("pool running");
    Check(ChunksOnce(1000, 8), "1000 items in 8 chunks");
    Check(ChunksOnce(7, 8), "7 items in 8 chunks");
    Check(ChunksOnce(1, 8), "1 item");
    Check(ChunksOnce(100000, 3), "100000 items in 3 chunks");
    {
        std::atomic<int> ok{0};
        pt::ParallelTask nested([&] { ok += ChunksOnce(50000, 8); });
        std::thread other([&] { ok += ChunksOnce(60000, 8); });
        ok += ChunksOnce(70000, 8);
        nested.Wait();
        other.join();
        Check(ok == 3, "chunks from a task, a thread and the caller at once");
    }
    {
        std::atomic<int> runs{0};
        {
            pt::ParallelTask task([&] { runs++; });
            task.Wait();
            task.Wait();
            Check(task.Done(), "a task is done after Wait");
        }
        for (int i = 0; i < 100; ++i) {
            pt::ParallelTask task([&] { runs++; });
            if (i % 2) {
                task.Cancel();
            }
        }
        Check(runs >= 1 && runs <= 101, "a task runs at most once, cancelled or not");
    }
    std::printf("parallel_for: %d failures\n", failures);
    return failures ? 1 : 0;
}
