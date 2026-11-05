// Preserve the upstream v1.0.8 screen blend and near-black response verbatim.
// This is an authored encoded-space operation, not the sRGB transfer function.
// SDR display EOTF emulation is handled separately in composite_output.glsl.
vec3 ScreenTonemap(vec3 x, vec3 bloom) {
    vec3 x2 = x * x;
    vec3 r = clamp(x2 + bloom - x2 * bloom, 0.0, 1.0);
    return clamp(r * inversesqrt(max(vec3(1.0 / 512.0), r)), 0.0, 1.0);
}

// HDR bloom is additive radiance before grading and display mapping. The old
// screen blend dimmed highlights, then changed slope separately in R/G/B at .5.
vec3 ScreenTonemapHdr(vec3 x, vec3 bloom) {
    vec3 encoded = SrgbEncode(SrgbDecode(max(x, vec3(0.0))) + max(bloom, vec3(0.0)));
    // The same vanilla toe, without the SDR ceiling. Above the knee this is
    // identity, preserving extended highlights for the later display mapping.
    const float knee = 0.04419417382415922; // sqrt(1/512), in encoded space.
    return encoded * (encoded / max(vec3(knee), encoded));
}
