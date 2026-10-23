#pragma once

#include <cstdint>
#include <string>

namespace pt::vk {

constexpr uint32_t kIntelVendorId = 0x8086;
constexpr uint32_t kIntelFirstGoodDriver = (101u << 14) | 2145u;

std::string IntelWindowsDriverName(uint32_t driver_version);

bool IntelDriverCrashesFirstFrame(uint32_t vendor_id, uint32_t driver_version, bool windows);

}
