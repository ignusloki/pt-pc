#version 460
#include "common.glsl"
#include "lens_mapping.glsl"

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

void main() {
    vec2 size = pass.f0.xy;
    vec2 render = pass.f0.zw;
    vec2 source = clamp(LensSource(in_uv, size, 1.275), 0.0, 1.0);
    ivec2 texel = clamp(ivec2(source * render), ivec2(0), ivec2(render) - 1);
    float depth = ImgFetch(IMG_DEPTH, texel).x;
    vec2 motion = ImgFetch(IMG_MOTION, texel).xy;
    vec2 previous = source + motion;
    vec2 shown = in_uv + motion / 0.9;
    for (int i = 0; i < 4; ++i) {
        shown += (previous - LensSource(shown, size, 1.275)) / 0.9;
    }
    out_motion = vec4(shown - in_uv, 0.0, 0.0);
    gl_FragDepth = depth;
}
