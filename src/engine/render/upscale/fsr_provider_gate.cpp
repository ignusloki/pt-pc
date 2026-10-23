#include "engine/render/upscale/fsr_provider_gate.h"

namespace pt {

std::vector<int> ParseFsrVersion(std::string_view name) {
    std::vector<int> parts;
    for (size_t at = 0; at < name.size();) {
        size_t end = at;
        int value = 0;
        while (end < name.size() && name[end] >= '0' && name[end] <= '9' && end - at < 9) {
            value = value * 10 + (name[end] - '0');
            ++end;
        }
        if (end == at) break;
        parts.push_back(value);
        at = end < name.size() && name[end] == '.' ? end + 1 : name.size();
    }
    return parts;
}

FsrGateResult DecideFsrProvider(const FsrGateInput& in) {
    FsrGateResult out;
    std::vector<int> best;
    for (const FsrProvider& p : in.providers) {
        out.listed += (out.listed.empty() ? "" : ", ") + p.name;
        const std::vector<int> parts = ParseFsrVersion(p.name);
        if (!parts.empty() && parts[0] == in.generation && parts > best) {
            best = parts;
            out.id = p.id;
            out.name = p.name;
        }
    }
    if (out.listed.empty()) out.listed = in.query_ok ? "none" : "query failed";
    const bool found = in.query_ok && out.id != 0;
    if (in.generation != 4) {
        out.available = found;
        return out;
    }
    if (in.api == FsrApi::Vulkan && !in.vulkan_sdk2 && !found) {
        out.reason = kFsr4ReasonVulkan;
    } else if (in.vendor_id != kFsrAmdVendorId) {
        out.reason = kFsr4ReasonNotAmd;
    } else if (!found) {
        out.reason = kFsr4ReasonDriver;
    } else {
        out.available = true;
        return out;
    }
    out.id = 0;
    out.name.clear();
    return out;
}

bool FsrCreatedIsGeneration(std::string_view created_name, int generation) {
    const std::vector<int> parts = ParseFsrVersion(created_name);
    return !parts.empty() && parts[0] == generation;
}

const char* FsrReasonNote(std::string_view reason) {
    if (reason == kFsr4ReasonVulkan) return "pc_note_fsr4_vulkan";
    if (reason == kFsr4ReasonNotAmd) return "pc_note_fsr4_amd";
    if (reason == kFsr4ReasonDriver) return "pc_note_fsr4_driver";
    return nullptr;
}

}
