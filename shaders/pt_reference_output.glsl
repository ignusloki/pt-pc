// Shared verbatim by the Original and Enhanced shader families.
void main() {
    v = frame.views[pass.ids.x];
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(v.viewport.xy);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }
    uint rng = RtHash(uint(pixel.x) * 1973u + uint(pixel.y) * 9277u) ^ RtHash(pass.ids.z * 26699u + 17u);
    vec4 sum = pass.ids.w != 0u ? vec4(0.0) : imageLoad(pt_reference_accum, pixel);
    uint samples = max(uint(pass.f0.x + 0.5), 1u);
    for (uint i = 0u; i < samples; ++i) {
        vec2 frag = vec2(pixel) + vec2(RtRandom(rng), RtRandom(rng));
        vec3 c = CameraPath(frag, rng);
        if (!any(isnan(c)) && !any(isinf(c))) {
            sum += vec4(c, 1.0);
        }
    }
    imageStore(pt_reference_accum, pixel, sum);
}
