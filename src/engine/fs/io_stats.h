#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pt {

struct IoTotals {
    uint32_t reads = 0;
    uint64_t bytes = 0;
    double read_ms = 0.0;
    double lock_ms = 0.0;
};

void IoSetMainThread();
bool IoOnMainThread();
void IoSimulatedLatency();
void IoRecord(size_t bytes, double read_ms, double lock_ms);
IoTotals IoTakeMain();
bool IoTraceOn();
void IoTrace(const char* kind, std::string_view name, size_t bytes, double ms);

}
