// RenoDX effects::ApplyFilmGrain / ComputeFilmGraininess (MIT).
// Copyright (c) 2025 Carlos Lopez Jr. See docs/hdr-post-licenses.txt.
// Input/output: linear BT.709, 1 = reference white. Grain modulates luminance
// without changing chromaticity or adding a black pedestal.
vec3 PerceptualFilmGrain(vec3 color, vec2 pixel, vec2 seed, float strength, float peak, float wide) {
    if (strength <= 0.0) return color;
    float y = max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.0);
    if (y <= 0.0) return color;
    float density = min(y * 3.0, 10.0);
    float graininess = pow(10.0, 0.880 - 0.736 * density - 0.003 * pow(density, 7.6));
    // RenoDX's Hash33 family avoids the large sin() multiplier's quantization.
    vec3 p = fract(vec3(pixel + seed * 4096.0, seed.x + seed.y) * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    float noise = fract((p.x + p.y) * p.z) * 2.0 - 1.0;
    float amplitude = min(graininess * strength * 0.04 * 1.667, 0.99);
    // Symmetric headroom keeps the noise unbiased at the display boundary.
    float maximum = GamutPeak(color, wide);
    amplitude = min(amplitude, max(peak / max(maximum, 1e-8) - 1.0, 0.0));
    return color * (1.0 + noise * amplitude);
}
