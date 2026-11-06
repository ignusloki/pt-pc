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

float Hash(float n) {
    return fract(sin(n * 12.9898) * 43758.5453);
}

float Covered(ivec2 p, ivec2 size) {
    return ImgFetch(IMG_DEPTH, clamp(p, ivec2(0), size - 1)).x > 1.0e-6 ? 1.0 : 0.0;
}
