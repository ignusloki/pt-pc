#ifndef PT_RADIANCE_GLSL
#define PT_RADIANCE_GLSL

// Legacy material/storage ceilings are not display mapping. Keep HDR radiance
// until the output shoulder; only guard the finite FP16 storage range here.
vec3 SceneRadiance(vec3 color, float legacy_limit, bool hdr) {
    if (!hdr) return min(color, vec3(legacy_limit));
    if (any(isnan(color)) || any(isinf(color))) return vec3(0.0);
    color = max(color, vec3(0.0));
    float peak = max(color.r, max(color.g, color.b));
    return peak > 60000.0 ? color * (60000.0 / peak) : color;
}

#endif
