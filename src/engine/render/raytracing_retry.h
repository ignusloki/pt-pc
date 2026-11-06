#pragma once

#include <chrono>
#include "engine/core/log.h"

namespace pt {

inline bool RayTracingReady(bool valid, const std::chrono::steady_clock::time_point& retry_after) {
    return valid || std::chrono::steady_clock::now() >= retry_after;
}

inline void RayTracingBackOff(std::chrono::steady_clock::time_point& retry_after, const char* what) {
    const bool first = std::chrono::steady_clock::now() >= retry_after;
    retry_after = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    if (first) LogWarn("ray tracing: {} not allocated (out of video memory), tried again in 1 s", what);
}

}
