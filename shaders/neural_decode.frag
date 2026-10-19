#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 nr = clamp(ImgFetch(IMG_NEURAL_OUT, p).rgb, vec3(0.0), vec3(0.999999));
    float alpha = ImgFetch(IMG_NEURAL_IN, p).a;
    vec3 c = nr / max(vec3(1.0) - nr, vec3(1.0e-6));
    if (pass.f0.y > 1.0) {
        vec3 proxy = clamp(ImgFetch(IMG_NEURAL_IN, p).rgb, vec3(0.0), vec3(0.999999));
        vec3 original = proxy / max(vec3(1.0) - proxy, vec3(1.0e-6));
        const vec3 luma = vec3(0.212639, 0.715169, 0.072192);
        float before_y = dot(original, luma), after_y = dot(c, luma);
        float boosted_y = max(before_y + (after_y - before_y) * min(pass.f0.y, 3.0), 0.0);
        c *= boosted_y / max(after_y, 1.0e-6);
    }
    out_color = vec4(c / max(pass.f0.x, 1.0e-6), alpha);
}
