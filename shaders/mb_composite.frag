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
    out_color = Img(pass.ids.x, SMP_LINEAR_CLAMP, pass.f0.zw * in_uv + pass.f0.xy);
    if (pass.f1.y > 0.0) {
        out_color.rgb = pow(max(out_color.rgb, vec3(0.0)), vec3(pass.f1.y));
    }
    if (pass.f1.x > 0.0) {
        out_color.rgb = pow(max(out_color.rgb, vec3(0.0)), vec3(1.0 / pass.f1.x));
    }
}
