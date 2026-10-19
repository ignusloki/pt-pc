#include "engine/render/gpu_types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using uint = unsigned;
static constexpr uint MAT_ALPHA_TEST = pt::gpu::kMatAlphaTest;
static constexpr uint MAT_TRANSLUCENT_TEX = pt::gpu::kMatTranslucentTex;
static constexpr uint MAT_ALPHA_DITHER = pt::gpu::kMatAlphaDither;

static float clamp(float value, float lo, float hi) {
    return std::clamp(value, lo, hi);
}

#include "../shaders/pt_foliage_shadow.glsl"

int main() {
    int failures = 0;
    unsigned cases = 0;
    const uint leaf = MAT_ALPHA_TEST | MAT_TRANSLUCENT_TEX;
    auto check = [&](const char* label, float expected, float translucency, float strength, uint material, uint record, bool authored_shadow) {
        const float value = PtFoliageShadowTransmission(translucency, strength, material, record, authored_shadow);
        if (!std::isfinite(value) || std::abs(value - expected) > 1.0e-6f) {
            std::printf("FAIL %s: expected %.6f, got %.6f\n", label, expected, value);
            ++failures;
        }
        ++cases;
    };
    check("half-translucent static leaf", 0.5f, 0.5f, 1.0f, leaf, 8u, false);
    check("unused record bits do not erase leaf shadows", 0.5f, 0.5f, 1.0f, leaf, 8u | 2048u, false);
    check("unused record bits do not make opaque leaf texels transparent", 0.0f, 0.0f, 1.0f, leaf, 8u | 2048u, false);
    check("unused record bits retain authored leaf shadows", 0.0f, 0.5f, 1.0f, leaf, 8u | 2048u, true);
    check("unused record bits retain indirect visibility", 0.0f, 0.5f, 0.0f, leaf, 8u | 2048u, false);
    check("unused record bits do not affect skin", 0.0f, 0.5f, 1.0f, leaf, 8u | 4u | 2048u, false);
    check("transmission cap preserves a shadow", 0.75f, 1.0f, 1.0f, leaf, 8u, false);
    check("adjustable strength", 0.4f, 0.8f, 0.5f, leaf, 8u, false);
    check("zero strength restores opaque shadow", 0.0f, 0.8f, 0.0f, leaf, 8u, false);
    check("opaque texture remains opaque", 0.0f, 0.0f, 1.0f, leaf, 8u, false);
    check("negative texture remains opaque", 0.0f, -0.5f, 1.0f, leaf, 8u, false);
    check("strength bounded", 0.5f, 0.5f, 2.0f, leaf, 8u, false);
    check("opaque surface excluded", 0.0f, 1.0f, 1.0f, MAT_TRANSLUCENT_TEX, 0u, false);
    check("ordinary alpha excluded", 0.0f, 1.0f, 1.0f, MAT_ALPHA_TEST, 8u, false);
    check("dithered hair excluded", 0.0f, 1.0f, 1.0f, leaf | MAT_ALPHA_DITHER, 8u, false);
    check("skinned alpha excluded", 0.0f, 1.0f, 1.0f, leaf, 8u | 4u, false);
    check("authored shadow light excluded", 0.0f, 1.0f, 1.0f, leaf, 8u, true);
    check("fixture owner bits do not exclude foliage", 0.5f, 0.5f, 1.0f, leaf, 11u | (7u << 4u), false);
    check("nonfinite control rejected", 0.0f, 0.8f, NAN, leaf, 8u, false);
    check("nonfinite texture rejected", 0.0f, NAN, 1.0f, leaf, 8u, false);
    std::printf("foliage shadow transmission: %u cases, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
