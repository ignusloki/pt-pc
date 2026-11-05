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

vec3 SampleGrade(uint lut, vec3 c) {
    int size = int(ImgSize(lut).y);
    float last_index = float(size-1);
    if (pass.f1.z > 0.5) {
        vec3 p = clamp(c, 0.0, 1.0)*last_index;
        ivec3 base = min(ivec3(floor(p)), ivec3(size-2));
        vec3 f = p - vec3(base);
        // Sort fractional coordinates; simplex barycentric weights are positive.
        ivec3 axis = ivec3(0,1,2);
        if (f[axis.x] < f[axis.y]) { int t=axis.x; axis.x=axis.y; axis.y=t; }
        if (f[axis.y] < f[axis.z]) { int t=axis.y; axis.y=axis.z; axis.z=t; }
        if (f[axis.x] < f[axis.y]) { int t=axis.x; axis.x=axis.y; axis.y=t; }
        ivec3 a=base, b=base;
        a[axis.x]++; b=a; b[axis.y]++;
        vec3 v0=ImgFetch(lut, ivec2(base.x+base.z*size,base.y)).rgb;
        vec3 v1=ImgFetch(lut, ivec2(a.x+a.z*size,a.y)).rgb;
        vec3 v2=ImgFetch(lut, ivec2(b.x+b.z*size,b.y)).rgb;
        ivec3 last=base+1;
        vec3 v3=ImgFetch(lut, ivec2(last.x+last.z*size,last.y)).rgb;
        return v0*(1.0-f[axis.x])+v1*(f[axis.x]-f[axis.y])+v2*(f[axis.y]-f[axis.z])+v3*f[axis.z];
    }
    float slice = floor(last_index * c.b);
    float u = (c.r * last_index + 0.5 + slice * float(size)) / float(size*size);
    float v = (c.g * last_index + 0.5) / float(size);
    vec3 a = Img(lut, SMP_LINEAR_CLAMP, vec2(u, v)).rgb;
    vec3 b = Img(lut, SMP_LINEAR_CLAMP, vec2(u + 1.0 / float(size), v)).rgb;
    return mix(a, b, last_index * c.b - slice);
}

vec3 QuantizeDither(vec3 c, float salt) {
    vec3 seed = fract((vec3(gl_FragCoord.xyx) + salt) * vec3(0.1031, 0.1030, 0.0973));
    seed += dot(seed, seed.yxz + 33.33);
    return c + (fract((seed.x + seed.y) * seed.z) - 0.5) / 255.0;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 c;
    bool hdr_output = pass.f1.y > 0.0;
    if (pass.ids.y == 1u) {
        c = ImgFetch(pass.ids.x, pixel).rgb;
    } else {
        vec3 hdr = ImgFetch(IMG_HDR, pixel).rgb * pass.f0.z;
        hdr = mix(hdr, vec3(0.0), isnan(hdr));
        hdr = clamp(hdr, vec3(0.0), vec3(65504.0));
        vec3 flare = max(ImgFetch(IMG_FLARE, pixel).rgb, vec3(0.0));
        vec3 x = hdr_output ? SrgbEncode(SceneRadiance(hdr + flare, 1.0, true)) : SrgbEncode(min(hdr, vec3(1.0))) + flare;
        vec2 bloom_uv = hdr_output ? in_uv : in_uv + 0.5 / ImgSize(IMG_BLOOM_SUM);
        vec3 b = pass.f0.x > 0.0 ? max(Img(IMG_BLOOM_SUM, SMP_LINEAR_CLAMP, bloom_uv).rgb, vec3(0.0)) * pass.f0.x : vec3(0.0);
        if (!hdr_output) b = min(b, vec3(0.5));
        c = hdr_output ? ScreenTonemapHdr(x, b) : ScreenTonemap(min(x, vec3(1.0)), b);
    }
    vec3 graded = c;
    if (pass.f0.w > 0.0) {
        // SDR LUTs can flatten upper midtones before input reaches 1 (the
        // generated LUT also bakes color_scale). Preserve <=18% exactly; N2
        // compress the remaining max-channel range into an interior proxy,
        // then restore its scale after grading. Identity grades reconstruct RGB.
        vec3 linear_color = SrgbDecode(max(c, vec3(0.0)));
        float peak = max(linear_color.r, max(linear_color.g, linear_color.b));
        float scale = 1.0;
        if (hdr_output && peak > 0.18) {
            float excess = peak - 0.18;
            float proxy = 0.18 + excess / sqrt(1.0 + (excess / 0.32) * (excess / 0.32));
            scale = peak / proxy;
        }
        vec3 within = hdr_output ? SrgbEncode(linear_color / scale) : clamp(c, 0.0, 1.0);
        graded = SampleGrade(RES_COLOR_LUT, within);
        if (pass.f0.y < 1.0) {
            graded = mix(SampleGrade(RES_COLOR_LUT_PREV, within), graded, pass.f0.y);
        }
        if (hdr_output && scale > 1.0) {
            graded = SrgbEncode(SrgbDecode(max(graded, vec3(0.0))) * scale);
        }
    }
    out_color = vec4(pass.f1.x > 0.0 ? QuantizeDither(graded, pass.ids.y == 1u ? 17.0 : 0.0) : graded, Luma601(graded));
}
