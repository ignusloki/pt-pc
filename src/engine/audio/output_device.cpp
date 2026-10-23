#include "engine/audio/output_device.h"

#include <format>

#include "engine/platform/controller_speaker.h"

namespace pt::audio {

OutputSelection SelectPlaybackEndpoint(std::span<const PlaybackEndpoint> endpoints, std::string_view chosen) {
    OutputSelection out;
    for (size_t i = 0; i < endpoints.size(); ++i) {
        if (endpoints[i].system_default && !out.system_default) out.system_default = i;
    }
    if (chosen.empty()) return out;
    for (size_t i = 0; i < endpoints.size(); ++i) {
        if (endpoints[i].name == chosen) {
            out.pick = OutputPick::Chosen;
            out.endpoint = i;
            return out;
        }
    }
    out.pick = OutputPick::ChosenMissing;
    return out;
}

ControllerEndpoint ControllerEndpointKind(std::string_view name) {
    if (name.find("Xbox") != std::string_view::npos || name.find("XBOX") != std::string_view::npos) return ControllerEndpoint::None;
    if (IsSonyControllerEndpointName(SonyPadAudio::DualSense, name)) return ControllerEndpoint::DualSense;
    if (IsSonyControllerEndpointName(SonyPadAudio::DualShock4, name)) return ControllerEndpoint::DualShock4;
    return ControllerEndpoint::None;
}

std::string_view ControllerEndpointLabel(ControllerEndpoint kind) {
    switch (kind) {
        case ControllerEndpoint::DualSense: return "DualSense";
        case ControllerEndpoint::DualShock4: return "DualShock 4";
        default: return "none";
    }
}

std::string_view OutputPickLabel(OutputPick pick) {
    switch (pick) {
        case OutputPick::Chosen: return "chosen in the settings";
        case OutputPick::ChosenMissing: return "the chosen device is not connected, so the system default";
        default: return "the system default";
    }
}

std::string DescribePlaybackEndpoints(std::span<const PlaybackEndpoint> endpoints) {
    std::string out;
    for (const PlaybackEndpoint& endpoint : endpoints) {
        if (!out.empty()) out += "; ";
        out += std::format("'{}' ({} ch", endpoint.name, endpoint.channels);
        if (!endpoint.id.empty()) out += ", id " + endpoint.id;
        if (endpoint.system_default) out += ", system default";
        if (const ControllerEndpoint kind = ControllerEndpointKind(endpoint.name); kind != ControllerEndpoint::None) {
            out += std::format(", {} controller", ControllerEndpointLabel(kind));
        }
        out += ")";
    }
    return out.empty() ? std::string("none") : out;
}

}
