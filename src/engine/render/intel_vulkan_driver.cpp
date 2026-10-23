#include "engine/render/intel_vulkan_driver.h"

#include <format>

namespace pt::vk {

std::string IntelWindowsDriverName(uint32_t driver_version) {
    return std::format("31.0.{}.{}", driver_version >> 14, driver_version & 0x3fffu);
}

bool IntelDriverCrashesFirstFrame(uint32_t vendor_id, uint32_t driver_version, bool windows) {
    if (!windows || vendor_id != kIntelVendorId) {
        return false;
    }
    const uint32_t branch = driver_version >> 14;
    return (branch == 100 || branch == 101) && driver_version < kIntelFirstGoodDriver;
}

}
