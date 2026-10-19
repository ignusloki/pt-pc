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

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = ImgFetch(IMG_DEPTH, pixel).x;
    if (depth <= 0.0) {
        discard;
    }
    if (pass.ids.z == 1u) {
        View v = frame.views[pass.ids.x];
        vec4 g = ImgFetch(IMG_MATERIAL, pixel);
        vec3 level = g.y * Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(g.z, 0.25)).rgb;
        vec4 glossy = imageLoad(rt_glossy_raw, pixel);
        out_diffuse = vec4(max(imageLoad(rt_gi_raw, pixel).rgb * v.exposure.x, vec3(0.0)), 0.0);
        out_specular = vec4(glossy.a >= 0.0 ? max(glossy.rgb * level * v.exposure.x, vec3(0.0)) : vec3(0.0), 0.0);
        return;
    }
    vec4 lit = imageLoad(rt_full_unshadowed, pixel);
    vec4 traced = imageLoad(rt_full_shadowed, pixel);
    float share = lit.a > 1.0e-8 ? clamp(traced.a / lit.a, 0.0, 1.0) : 1.0;
    out_diffuse = vec4(max(traced.rgb, vec3(0.0)), 0.0);
    out_specular = vec4(max(imageLoad(pt_spec_unshadowed, pixel).rgb * share, vec3(0.0)), 0.0);
}
