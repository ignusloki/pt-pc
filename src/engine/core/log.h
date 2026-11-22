#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace pt {

#ifdef PT_NO_RUNTIME_LOGS
inline constexpr bool kRuntimeLogs = false;
#else
inline constexpr bool kRuntimeLogs = true;
#endif

enum class LogLevel { Debug, Info, Warn, Error };

void LogWrite(LogLevel level, std::string_view text);
void LogSetFile(const std::filesystem::path& path);
bool LogTryWrite(LogLevel level, std::string_view text);
void LogSetTick(uint64_t tick);
double LogClock();

template <typename... Args>
void LogDebug(std::format_string<Args...> fmt, Args&&... args) {
    if constexpr (kRuntimeLogs) LogWrite(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void LogInfo(std::format_string<Args...> fmt, Args&&... args) {
    if constexpr (kRuntimeLogs) LogWrite(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void LogWarn(std::format_string<Args...> fmt, Args&&... args) {
    if constexpr (kRuntimeLogs) LogWrite(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void LogError(std::format_string<Args...> fmt, Args&&... args) {
    if constexpr (kRuntimeLogs) LogWrite(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

}
