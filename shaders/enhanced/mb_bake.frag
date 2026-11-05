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
    View v = frame.views[pass.ids.w];
    vec2 velocity = Img(pass.ids.x, SMP_POINT_CLAMP, in_uv).zw;
    float depth = Img(IMG_DEPTH, SMP_POINT_CLAMP, in_uv).x;
    vec2 tile = Img(pass.ids.y, SMP_POINT_CLAMP, in_uv).zw;
    vec2 scaled = 128.0 * velocity * pass.f0.xy * pass.f0.w;
    vec2 dominant = dot(tile, tile) >= dot(velocity, velocity) ? tile : velocity;
    out_color = vec4(0.015625 * sqrt(1.0e-7 + dot(scaled, scaled)), min(abs(ViewZ(v, depth)), 65000.0), dominant);
}
