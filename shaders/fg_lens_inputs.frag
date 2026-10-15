#version 460
#include "common.glsl"

#define IMG_MOTION 45

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_motion;

vec2 LensSource(vec2 uv, vec2 size) {
    float across = (size.x / size.y) / (16.0 / 9.0);
    vec2 texel = vec2(1.0 / (1080.0 * size.x / size.y), 1.0 / 1080.0);
    vec2 p = (uv * 2.0 - 1.0) * vec2(across, 1.0);
    float r3 = Saturate(length(p) * 0.52240777);
    float r5 = 1.0 - sin(1.5707964 * r3);
    float r6 = inversesqrt(dot(p, p) + r5 * r5);
    float r7 = pow(r3, 1.75);
    vec2 offset = r7 * 84.0 * texel * p * r6;
    return uv * 0.9 + 0.050000012 + offset * 1.275;
}

void main() {
    vec2 size = pass.f0.xy;
    vec2 render = pass.f0.zw;
    vec2 source = clamp(LensSource(in_uv, size), 0.0, 1.0);
    ivec2 texel = clamp(ivec2(source * render), ivec2(0), ivec2(render) - 1);
    float depth = ImgFetch(IMG_DEPTH, texel).x;
    vec2 motion = ImgFetch(IMG_MOTION, texel).xy;
    vec2 previous = source + motion;
    vec2 shown = in_uv + motion / 0.9;
    for (int i = 0; i < 4; ++i) {
        shown += (previous - LensSource(shown, size)) / 0.9;
    }
    out_motion = vec4(shown - in_uv, 0.0, 0.0);
    gl_FragDepth = depth;
}
