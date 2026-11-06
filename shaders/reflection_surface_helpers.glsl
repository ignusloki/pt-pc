// Shared verbatim by the Original and Enhanced shader families.
#extension GL_EXT_ray_query : require
#ifndef PT_D3D12
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#endif
#include "common.glsl"
#include "rt_shadow.glsl"
#include "reflection_mix.glsl"
#include "reflection_depth.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_map;
layout(location = 1) out vec4 out_color;

const float kReach = 50.0;

View v;

float SceneZ(vec2 uv) {
    return ViewZ(v, ReflectionFloorDepth(v, uv));
}

#include "reflection_projection.glsl"



bool LightHidden(uint i) {
    uint word = i >> 5u;
    return (floatBitsToUint(pass.m[word >> 2u][word & 3u]) & (1u << (i & 31u))) != 0u;
}

vec3 EvaluateSh(Probe p, vec3 n) {
    vec3 e = p.sh[0].rgb;
    e += p.sh[1].rgb * n.y + p.sh[2].rgb * n.z + p.sh[3].rgb * n.x;
    e += p.sh[4].rgb * (n.x * n.y) + p.sh[5].rgb * (n.y * n.z) + p.sh[6].rgb * (3.0 * n.z * n.z - 1.0);
    e += p.sh[7].rgb * (n.x * n.z) + p.sh[8].rgb * (n.x * n.x - n.y * n.y);
    return e;
}
