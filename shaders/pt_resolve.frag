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

#include "rt_upsample.glsl"

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
