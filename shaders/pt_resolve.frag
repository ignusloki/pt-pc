#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) out vec4 out_diffuse;
layout(location = 1) out vec4 out_specular;

#define RT_FULL_IMAGES_READONLY 1
#include "rt_full_images.glsl"
#define PT_IMAGES_READONLY 1
#include "pt_images.glsl"

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
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = ImgFetch(IMG_DEPTH, pixel).x;
    if (depth <= 0.0) {
        discard;
    }
    if (pass.ids.z >= 2u) {
        float code = imageLoad(pt_layer_raw, pixel).a;
        float kind = floor(code * 0.5);
        float amount = kind >= 1.0 ? Saturate(code - 2.0 * kind) : 0.0;
        if (pass.ids.z == 2u) {
            out_diffuse = vec4(1.0 - amount);
            out_specular = vec4(1.0 - amount);
            return;
        }
        View v = frame.views[pass.ids.x];
        vec3 light = imageLoad(pt_layer_raw, pixel).rgb;
        out_diffuse = vec4(0.0);
        out_specular = vec4(max(light * amount * v.exposure.x, vec3(0.0)), 0.0);
        return;
    }
    if (pass.ids.z == 1u) {
        View v = frame.views[pass.ids.x];
        ivec2 size = ivec2(v.viewport.xy);
        float z = ViewZ(v, depth);
        vec3 n = DecodeNormal(ImgFetch(IMG_NORMAL, pixel).xyz);
        vec4 g = ImgFetch(IMG_MATERIAL, pixel);
        vec3 level = g.y * Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(g.z, 0.25)).rgb;
        out_diffuse = vec4(max(Upsample(false, pixel, size, z, n, v) * v.exposure.x, vec3(0.0)), 0.0);
        out_specular = vec4(max(Upsample(true, pixel, size, z, n, v) * level * v.exposure.x, vec3(0.0)), 0.0);
        return;
    }
    vec4 ratio = (pass.ids.w & 4u) != 0u ? imageLoad(rt_ratio_history1, pixel) : imageLoad(rt_ratio_history0, pixel);
    vec3 diffuse = imageLoad(rt_full_unshadowed, pixel).rgb * ratio.rgb;
    vec3 specular = imageLoad(pt_spec_unshadowed, pixel).rgb * ratio.a;
    out_diffuse = vec4(max(diffuse, vec3(0.0)), 0.0);
    out_specular = vec4(max(specular, vec3(0.0)), 0.0);
}
