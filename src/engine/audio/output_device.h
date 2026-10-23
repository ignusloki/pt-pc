#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pt::audio {

struct PlaybackEndpoint {
    std::string name;
    std::string id;
    int channels = 0;
    bool system_default = false;
};

enum class OutputPick : uint8_t {
    SystemDefault,
    Chosen,
    ChosenMissing,
};

struct OutputSelection {
    OutputPick pick = OutputPick::SystemDefault;
    std::optional<size_t> endpoint;
    std::optional<size_t> system_default;
};

OutputSelection SelectPlaybackEndpoint(std::span<const PlaybackEndpoint> endpoints, std::string_view chosen);

enum class ControllerEndpoint : uint8_t { None, DualSense, DualShock4 };
ControllerEndpoint ControllerEndpointKind(std::string_view name);
std::string_view ControllerEndpointLabel(ControllerEndpoint kind);

std::string DescribePlaybackEndpoints(std::span<const PlaybackEndpoint> endpoints);
std::string_view OutputPickLabel(OutputPick pick);

}
