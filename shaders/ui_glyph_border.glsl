float GlyphCoverage(UiDraw d, vec2 uv) {
    bvec4 inside = bvec4(greaterThanEqual(uv, in_color.xy), lessThanEqual(uv, in_color.zw));
    return all(inside) ? Sample(d.textures.x, uv).r : 0.0;
}

vec4 ShadeBorder(UiDraw d) {
    vec2 dx = dFdxFine(in_uv) * d.extra.z;
    vec2 dy = dFdyFine(in_uv) * d.extra.z;
    vec2 lo = -d.extra.xy;
    vec2 hi = d.extra.xy;
    if (d.extra.w > 0.5) {
        float center = GlyphCoverage(d, in_uv);
        float n0 = GlyphCoverage(d, in_uv + clamp(-1.2 * dy, lo, hi));
        float n1 = GlyphCoverage(d, in_uv + clamp(-1.2 * dx, lo, hi));
        float n2 = GlyphCoverage(d, in_uv + clamp(1.2 * dx, lo, hi));
        float n3 = GlyphCoverage(d, in_uv + clamp(1.2 * dy, lo, hi));
        // Android: composite the white glyph over its black rim using the atlas's continuous coverage.
        // UiBlend::Alpha expects straight colour, so divide the premultiplied fill by the combined alpha.
        float outline = max(max(n0, n1), max(n2, n3));
        float alpha = center + outline * (1.0 - center);
        vec3 rgb = alpha > 0.0 ? d.m[0].rgb * center / alpha : vec3(0.0);
        return vec4(rgb, alpha * d.m[0].a);
    }
    vec2 px = dFdxFine(in_uv) * 0.3;
    vec2 py = dFdyFine(in_uv) * 0.3;
    float center = (GlyphCoverage(d, in_uv) + GlyphCoverage(d, in_uv + px + py) + GlyphCoverage(d, in_uv + px - py) + GlyphCoverage(d, in_uv - px + py) +
                    GlyphCoverage(d, in_uv - px - py)) * 0.2;
    center = max(center, GlyphCoverage(d, in_uv) * 0.85);
    float reach = 0.0;
    for (int i = 0; i < 12; ++i) {
        float a = float(i) * 0.52359878;
        vec2 dir = cos(a) * dx + sin(a) * dy;
        reach = max(reach, GlyphCoverage(d, in_uv + clamp(1.2 * dir, lo, hi)));
        if ((i & 1) == 0) reach = max(reach, GlyphCoverage(d, in_uv + clamp(0.6 * dir, lo, hi)));
    }
    float rim = smoothstep(0.0, 0.55, reach);
    float ink = smoothstep(0.18, 0.62, center);
    float alpha = 1.0 - (1.0 - ink) * (1.0 - rim);
    return vec4(vec3(ink / max(alpha, 0.0001)), alpha);
}
