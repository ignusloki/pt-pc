#ifndef PT_COLOR_GAMUT_GLSL
#define PT_COLOR_GAMUT_GLSL
// Linear D65 coordinates. P3 limiting gamut is independent of HDR transport.
vec3 ToLimitingGamut(vec3 rgb, float wide) {
    if (wide < 0.5) return rgb;
    return vec3(dot(rgb, vec3(0.822461969, 0.177538031, 0.0)),
                dot(rgb, vec3(0.033194199, 0.966805801, 0.0)),
                dot(rgb, vec3(0.017082631, 0.072397441, 0.910519929)));
}
vec3 FromLimitingGamut(vec3 rgb, float wide) {
    if (wide < 0.5) return rgb;
    return vec3(dot(rgb, vec3(1.224940176, -0.224940176, 0.0)),
                dot(rgb, vec3(-0.042056955, 1.042056955, 0.0)),
                dot(rgb, vec3(-0.019637555, -0.078636046, 1.098273600)));
}
float GamutPeak(vec3 rgb, float wide) {
    vec3 target = ToLimitingGamut(rgb, wide);
    return max(target.r, max(target.g, target.b));
}
// RenoDX neutral-axis compression extended with a smooth N2 boundary knee.
// Preserves linear luminance and D65 chromaticity-ray direction. This is not
// the perceptual JMh gamut mapper in ACES 2; see hdr-color-grading.md.
vec3 CompressGamut(vec3 rgb, float wide) {
    float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    if (y <= 0.0) return vec3(0.0);
    vec3 target = ToLimitingGamut(rgb, wide);
    float radius = max(0.0, 1.0 - min(target.r, min(target.g, target.b)) / y);
    if (radius <= 0.8) return rgb;
    float excess = (radius - 0.8) / 0.2;
    float mapped = 0.8 + 0.2 * excess / sqrt(1.0 + excess * excess);
    return vec3(y) + (rgb - vec3(y)) * (mapped / radius);
}
// Post-UI safety guard, not another soft compression. Identity inside the
// target cone; only round-off/overlay excursions are brought to its boundary.
vec3 LimitOutputGamut(vec3 rgb, float wide, float peak) {
    if (wide > 0.5) {
        float y = max(dot(rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0);
        vec3 target = ToLimitingGamut(rgb, wide);
        float lowest = min(target.r, min(target.g, target.b));
        if (lowest < 0.0) rgb = mix(vec3(y), rgb, y / max(y - lowest, 1e-20));
    } else {
        rgb = max(rgb, vec3(0.0));
    }
    float highest = GamutPeak(rgb, wide);
    return rgb * min(1.0, peak / max(highest, 1e-20));
}
#endif
