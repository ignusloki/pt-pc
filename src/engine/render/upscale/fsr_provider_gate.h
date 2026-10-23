#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pt {

inline constexpr uint32_t kFsrAmdVendorId = 0x1002;

enum class FsrApi { D3D12, Vulkan };

struct FsrProvider {
    uint64_t id = 0;
    std::string name;
};

struct FsrGateInput {
    int generation = 3;
    FsrApi api = FsrApi::D3D12;
    uint32_t vendor_id = 0;
    bool query_ok = false;
    bool vulkan_sdk2 = false;
    std::vector<FsrProvider> providers;
};

struct FsrGateResult {
    bool available = false;
    uint64_t id = 0;
    std::string name;
    std::string listed;
    std::string reason;
};

inline constexpr std::string_view kFsr4ReasonNotAmd = "FSR 4 needs an AMD Radeon GPU";
inline constexpr std::string_view kFsr4ReasonDriver =
    "AMD's driver offers no FSR 4 for this GPU (on a Radeon RX 6000 or newer, update AMD Software: Adrenalin Edition to 26.10.1 or newer)";
inline constexpr std::string_view kFsr4ReasonVulkan =
    "AMD has announced FSR 4 for Vulkan in an upcoming FSR SDK, which is not released yet, and this game renders with Vulkan";

std::vector<int> ParseFsrVersion(std::string_view name);

FsrGateResult DecideFsrProvider(const FsrGateInput& input);

bool FsrCreatedIsGeneration(std::string_view created_name, int generation);

const char* FsrReasonNote(std::string_view reason);

}
