// Lilium luminance RCAS, adapted from RenoDX's Crimson Desert / Alien Isolation.
// Based on AMD FidelityFX RCAS, Copyright (c) 2024 Advanced Micro Devices, Inc.
// MIT; see docs/hdr-post-licenses.txt. All inputs are display-linear BT.709.
vec3 HdrRcas(vec3 e, vec3 b, vec3 d, vec3 f, vec3 h, float strength, float peak, float wide) {
    if (strength <= 0.0) return e;
    vec3 weights = vec3(0.2126, 0.7152, 0.0722) / max(peak, 1.0);
    float ey = max(dot(e, weights), 0.0);
    vec4 ring = max(vec4(dot(b, weights), dot(d, weights), dot(f, weights), dot(h, weights)), 0.0);
    float lo = min(min(ring.x, ring.y), min(ring.z, ring.w));
    float hi = max(max(ring.x, ring.y), max(ring.z, ring.w));
    if (ey <= 1e-8 || lo >= 1.0) return e;
    float limited = min(hi, 0.99);
    float hit_min = lo / max(4.0 * limited, 1e-8);
    float hit_max = (1.0 - limited) / min(4.0 * lo - 4.0, -1e-8);
    float lobe = max(-0.1875, min(max(-hit_min, hit_max), 0.0)) * clamp(strength, 0.0, 1.0);
    float noise = clamp(abs(dot(ring, vec4(0.25)) - ey) / max(max(hi, ey) - min(lo, ey), 1e-8), 0.0, 1.0);
    lobe *= 1.0 - 0.5 * noise;
    float sharpened = (dot(ring, vec4(lobe)) + ey) / (4.0 * lobe + 1.0);
    float gain = clamp(sharpened / ey, 0.0, 4.0);
    // Luminance bounds alone cannot protect saturated RGB channels.
    gain = min(gain, peak / max(GamutPeak(e, wide), 1e-8));
    return e * gain;
}
