#include "engine/platform/ios_memory.h"
#include <cstdlib>
#include <string_view>
#if defined(__APPLE__) && (PT_IOS || PT_BC_DECODE_TEST)
#include <sys/sysctl.h>
#endif

namespace pt {
uint32_t IosTextureLimit() {
    static const uint32_t limit = [] {
#if PT_IOS || PT_BC_DECODE_TEST
        // Also available to the native decoder regression build for reproducing a phone's profile.
        if (const char* value = std::getenv("PT_IOS_TEXTURE_LIMIT")) {
            if (std::string_view(value) == "512") return 512u;
            if (std::string_view(value) == "1024") return 1024u;
        }
        uint64_t physical = 0;
#if defined(__APPLE__)
        size_t size = sizeof(physical);
        if (sysctlbyname("hw.memsize", &physical, &size, nullptr, 0) != 0) physical = 0;
#endif
        return IosTextureLimitForMemory(physical);
#else
        return 1024u;
#endif
    }();
    return limit;
}
}
