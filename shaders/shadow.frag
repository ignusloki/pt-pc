#version 460
#include "common.glsl"

layout(location = 0) in vec2 in_uv0;
layout(location = 1) in float in_hemisphere;

layout(push_constant) uniform DrawPush {
    mat4 model;
    uvec4 ids;
    vec4 tint;
} draw;

void main() {
    if (in_hemisphere < 0.0) {
        discard;
    }
    Material m = materials[draw.ids.y];
    if ((m.flags & MAT_ALPHA_TEST) != 0u) {
        float a = texture(PT_TEX2D(m.albedo), in_uv0).a;
        if (a < ShadowAlphaCutoff(m.flags)) {
            discard;
        }
    }
}
