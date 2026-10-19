#include "engine/render/rt_caster.h"

#include <cstdio>

int main() {
    int failures = 0;
    unsigned cases = 0;
    for (unsigned owner = 0; owner < 128; ++owner) {
    for (unsigned fields = 0; fields < 8; ++fields) {
        pt::RtCaster caster;
        caster.fitting = (fields & 1u) != 0;
        caster.alpha_test = (fields & 2u) != 0;
        caster.skin_base = (fields & 4u) != 0 ? 19u : pt::gpu::kInvalid;
        caster.light_fitting = owner;
        const uint32_t expected = (caster.fitting ? 1u | (caster.alpha_test ? 2u : 0u) : 0u) |
                                  (caster.skin_base != pt::gpu::kInvalid ? 4u : 0u) | (caster.alpha_test ? 8u : 0u) |
                                  ((owner & 63u) << 4);
        if (pt::RtRecordFlags(caster) != expected) ++failures;
        caster.mirror_hidden = true;
        const uint32_t hidden = pt::RtRecordFlags(caster);
        if ((hidden ^ expected) != 1024u || (hidden & 0xFFFF0000u) != 0u) ++failures;
        cases += 2;
    }
    }
    std::printf("ray record visibility flags: %u cases, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
