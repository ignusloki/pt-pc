#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <cstddef>

namespace pt::platform {

inline constexpr const char* kLegacyMovedMarker = "MOVED_TO.txt";
inline constexpr const char* kLegacyDeclinedMarker = "NOT_MOVED.txt";

enum class LegacyQuestion {
    Move,
    NewerCopies,
};

using LegacyAsk = std::function<std::optional<bool>(LegacyQuestion, const std::filesystem::path& legacy)>;

struct UserDataReport {
    bool success{};
    bool asked{};
    bool migrated_legacy{};
    bool cleaned_legacy{};
    bool removed_legacy{};
    bool declined_legacy{};
    bool legacy_pending{};
    bool legacy_failed{};
    bool legacy_busy{};
    std::size_t files_copied{};
    std::size_t files_backed_up{};
    std::size_t files_removed{};
    std::vector<std::string> errors;
    std::vector<std::string> legacy_errors;
    std::vector<std::string> warnings;
    std::vector<std::string> notes;
};

struct UserDataHooks {
    std::function<void(const std::filesystem::path& staged)> after_stage;
    std::function<void()> after_lock;
    std::optional<std::chrono::milliseconds> lock_wait;
};

inline constexpr std::chrono::milliseconds kLegacyLockWait{5000};

UserDataReport PrepareUserDataDirectory(const std::filesystem::path& destination,
                                       const std::filesystem::path& legacy,
                                       bool migrate_legacy = true,
                                       const LegacyAsk& ask = {},
                                       const UserDataHooks& hooks = {});

}
