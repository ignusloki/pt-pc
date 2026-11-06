// Shared verbatim by the Original and Enhanced shader families.
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

float BloomAlpha(vec3 color) {
    float r = Luma709(color) / 32.0;
    return r * inversesqrt(max(1.0 / 512.0, r));
}
