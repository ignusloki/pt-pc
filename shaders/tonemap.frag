#version 460
#include "common.glsl"
#include "tonemap_curve.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

vec3 Lut16(uint lut, vec3 c) {
    float slice = floor(15.0 * c.b);
    float u = c.r * (15.0 / 256.0) + 1.0 / 512.0 + slice / 16.0;
    float v = c.g * (15.0 / 16.0) + 1.0 / 32.0;
    vec3 a = Img(lut, SMP_LINEAR_CLAMP, vec2(u, v)).rgb;
    vec3 b = Img(lut, SMP_LINEAR_CLAMP, vec2(u + 1.0 / 16.0, v)).rgb;
    return mix(a, b, 15.0 * c.b - slice);
}

vec3 QuantizeDither(vec3 c, float salt) {
    vec3 seed = fract((vec3(gl_FragCoord.xyx) + salt) * vec3(0.1031, 0.1030, 0.0973));
    seed += dot(seed, seed.yxz + 33.33);
    return c + (fract((seed.x + seed.y) * seed.z) - 0.5) / 255.0;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 c;
    vec3 hdr = vec3(0.0);
    if (pass.ids.y == 1u) {
        c = ImgFetch(pass.ids.x, pixel).rgb;
    } else {
        hdr = ImgFetch(IMG_HDR, pixel).rgb * pass.f0.z;
        vec3 flare = pass.ids.z != 0u ? max(ImgFetch(IMG_FLARE, pixel).rgb, vec3(0.0)) : vec3(0.0);
        vec3 x = min(SrgbEncode(clamp(hdr, 0.0, 1.0)) + flare, vec3(1.0));
        vec2 bloom_uv = in_uv + 0.5 / ImgSize(IMG_BLOOM_SUM);
        vec3 b = pass.f0.x > 0.0 ? min(vec3(0.5), Img(IMG_BLOOM_SUM, SMP_LINEAR_CLAMP, bloom_uv).rgb) : vec3(0.0);
        c = ScreenTonemap(x, b);
    }
    vec3 graded = c;
    if (pass.f0.w > 0.0) {
        vec3 within = clamp(c, 0.0, 1.0);
        graded = Lut16(RES_COLOR_LUT, within);
        if (pass.f0.y < 1.0) {
            graded = mix(Lut16(RES_COLOR_LUT_PREV, within), graded, pass.f0.y);
        }
    }
    out_color = vec4(pass.f1.x > 0.0 ? QuantizeDither(graded, pass.ids.y == 1u ? 17.0 : 0.0) : graded, Luma601(graded));
}
