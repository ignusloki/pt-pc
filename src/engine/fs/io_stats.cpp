#include "engine/fs/io_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "engine/core/log.h"

namespace pt {

namespace {

std::thread::id g_main;
std::mutex g_mutex;
IoTotals g_main_totals;

int LatencyMs() {
    static const int ms = [] {
        const char* text = std::getenv("PT_IO_LATENCY_MS");
        return text ? std::max(0, std::atoi(text)) : 0;
    }();
    return ms;
}

}

void IoSetMainThread() {
    g_main = std::this_thread::get_id();
}

bool IoOnMainThread() {
    return std::this_thread::get_id() == g_main;
}

void IoSimulatedLatency() {
    if (const int ms = LatencyMs(); ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
}

void IoRecord(size_t bytes, double read_ms, double lock_ms) {
    if (!IoOnMainThread()) {
        return;
    }
    std::lock_guard lock(g_mutex);
    ++g_main_totals.reads;
    g_main_totals.bytes += bytes;
    g_main_totals.read_ms += read_ms;
    g_main_totals.lock_ms += lock_ms;
}

bool IoTraceOn() {
    static const bool on = [] {
        const char* text = std::getenv("PT_IO_LOG");
        return text && std::atoi(text) >= 2;
    }();
    return on && IoOnMainThread();
}

void IoTrace(const char* kind, std::string_view name, size_t bytes, double ms) {
    LogInfo("io: main thread {} {} ({:.1f} KB) in {:.1f} ms", kind, name, bytes / 1024.0, ms);
}

IoTotals IoTakeMain() {
    std::lock_guard lock(g_mutex);
    const IoTotals totals = g_main_totals;
    g_main_totals = {};
    return totals;
}

}
