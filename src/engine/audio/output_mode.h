#pragma once

#include <cstdint>
#include <string_view>

namespace pt::audio {

enum class OutputMode : uint8_t { Stereo, Surround, SpatialHeadphones };

constexpr std::string_view OutputModeName(OutputMode mode) {
    switch (mode) {
    case OutputMode::Surround: return "surround";
    case OutputMode::SpatialHeadphones: return "spatial";
    default: return "stereo";
    }
}

constexpr OutputMode ParseOutputMode(std::string_view name) {
    if (name == "surround") return OutputMode::Surround;
    if (name == "spatial") return OutputMode::SpatialHeadphones;
    return OutputMode::Stereo;
}

constexpr bool SpatialAudioBuilt() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

} // namespace pt::audio
