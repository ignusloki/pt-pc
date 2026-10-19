#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) out vec4 out_first;
layout(location = 1) out vec4 out_second;

#define RT_FULL_IMAGES_READONLY 1
#include "rt_full_images.glsl"

vec3 Upsample(bool glossy, ivec2 pixel, ivec2 size, float z, vec3 n, View v) {
    ivec2 half_size = (size + 1) / 2;
    vec2 at = vec2(pixel) * 0.5;
    ivec2 base = ivec2(floor(at));
    vec2 f = at - vec2(base);
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;
    vec3 nearest = vec3(0.0);
    float nearest_error = 1.0e9;
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i & 1, i >> 1);
        ivec2 p = min(base + o, half_size - 1);
        ivec2 full = min(p * 2, size - 1);
        float dk = ImgFetch(IMG_DEPTH, full).x;
        if (dk <= 0.0) {
            continue;
        }
        vec4 c = glossy ? imageLoad(rt_glossy_pong, p) : imageLoad(rt_gi_ping, p);
        if (c.a <= 0.0) {
            continue;
        }
        float error = abs(ViewZ(v, dk) - z) / z;
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) + 1.0e-3;
        w *= exp(-error / 0.02);
        w *= pow(max(dot(n, DecodeNormal(ImgFetch(IMG_NORMAL, full).xyz)), 0.0), 8.0);
        sum += w * c.rgb;
        weight_sum += w;
        if (error < nearest_error) {
            nearest_error = error;
            nearest = c.rgb;
        }
    }
    return weight_sum > 1.0e-4 ? sum / weight_sum : nearest;
}

void main() {
    View v = frame.views[pass.ids.x];
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size = ivec2(v.viewport.xy);
    float depth = ImgFetch(IMG_DEPTH, pixel).x;
    if (depth <= 0.0) {
        discard;
    }
    float z = ViewZ(v, depth);
    vec3 n = DecodeNormal(ImgFetch(IMG_NORMAL, pixel).xyz);
    if (pass.ids.z == 0u) {
        vec4 ratio = (pass.ids.w & 4u) != 0u ? imageLoad(rt_ratio_history1, pixel) : imageLoad(rt_ratio_history0, pixel);
        vec3 diffuse = imageLoad(rt_full_unshadowed, pixel).rgb * ratio.rgb;
        out_first = vec4(max(diffuse, vec3(0.0)), 0.0);
        out_second = vec4(vec3(ratio.a), 1.0);
        return;
    }
    if (pass.ids.z == 2u) {
        out_first = vec4(max(Upsample(false, pixel, size, z, n, v) * pass.f0.x * v.exposure.x, vec3(0.0)), 0.0);
        out_second = vec4(0.0);
        return;
    }
    vec4 g_material = ImgFetch(IMG_MATERIAL, pixel);
    float roughness = g_material.x;
    vec4 mat0 = Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(g_material.z, 0.25));
    vec4 mat1 = Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(g_material.z, 0.75));
    vec3 P = ViewPosition(v, PixelNdc(v, gl_FragCoord.xy), depth);
    float n_dot_v = Saturate(dot(n, -normalize(P)));
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    float environment = mat1.x * ab.x + ab.y;
    vec3 reflectance = g_material.y * mat0.rgb * environment * (1.0 - smoothstep(0.75 * pass.f0.z, pass.f0.z, roughness));
    vec3 glossy = Upsample(true, pixel, size, z, n, v);
    out_first = vec4(0.0);
    out_second = vec4(max(glossy * reflectance * pass.f0.y * v.exposure.x, vec3(0.0)), 0.0);
}
