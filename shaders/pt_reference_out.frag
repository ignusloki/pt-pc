#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) out vec4 out_color;

#define PT_IMAGES_READONLY 1
#include "pt_images.glsl"

void main() {
    View v = frame.views[pass.ids.x];
    vec4 sum = imageLoad(pt_reference_accum, ivec2(gl_FragCoord.xy));
    vec3 c = sum.a > 0.0 ? sum.rgb / sum.a : vec3(0.0);
    out_color = vec4(max(c * v.exposure.x, vec3(0.0)), 1.0);
}
