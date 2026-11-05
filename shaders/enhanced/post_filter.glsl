// Exact linear-light bilinear filtering of the extended sRGB post buffers.
// Decode each texel BEFORE interpolation; decoding a filtered code is too late.
vec4 OpticalLinearSample(uint source, vec2 uv) {
    vec2 p = uv * ImgSize(source) - 0.5;
    ivec2 a = ivec2(floor(p)), hi = ivec2(ImgSize(source)) - 1;
    vec2 f = fract(p);
    vec4 c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = ImgFetch(source, clamp(a + ivec2(i & 1, i >> 1), ivec2(0), hi));
        c[i].rgb = SrgbDecode(max(c[i].rgb, vec3(0.0)));
    }
    return mix(mix(c[0], c[1], f.x), mix(c[2], c[3], f.x), f.y);
}

vec4 PostLinearSample(uint source, vec2 uv) {
    if (frame.hdr_output.x <= 0.5) return Img(source, SMP_LINEAR_CLAMP, uv);
    if (frame.hdr_output.y > 0.5) {
        vec4 c = Img(source, SMP_LINEAR_CLAMP, uv);
        c.rgb = SrgbDecode(max(c.rgb, vec3(0.0)));
        return c;
    }
    return OpticalLinearSample(source, uv);
}
