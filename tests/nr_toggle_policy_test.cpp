#include "engine/render/upscale/neural_unlock.h"

#include <cstdio>

int main() {
    using pt::nr_unlock::GpuClass;
    using pt::nr_unlock::ToggleNeedsRestart;
    int failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
    };
    expect(!ToggleNeedsRestart(GpuClass::NeedsUnlock, true, true, false), "RTX 20/30/40 supports live changes");
    expect(!ToggleNeedsRestart(GpuClass::Native, true, true, false), "RTX 50 class alone never defers live changes");
    for (const auto gpu : {GpuClass::NeedsUnlock, GpuClass::Native}) {
        expect(ToggleNeedsRestart(gpu, true, true, true), "actual hook failure requires restart on either route class");
        expect(!ToggleNeedsRestart(gpu, false, true, true), "an unusable route does not queue a toggle restart");
        expect(!ToggleNeedsRestart(gpu, true, false, true), "a non-D3D12 route does not queue a toggle restart");
    }
    expect(!ToggleNeedsRestart(GpuClass::Other, true, true, true), "other GPU classification is not a community route");
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
