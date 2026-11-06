// Shared verbatim by the Original and Enhanced shader families.
#extension GL_EXT_ray_query : require
#ifndef PT_D3D12
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#endif
#include "common.glsl"
#include "rt_shadow.glsl"
#include "rt_full.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

#include "rt_full_images.glsl"

vec3 SampleVisibleNormal(vec3 v, float alpha, vec2 u) {
    vec3 vh = normalize(vec3(alpha * v.x, alpha * v.y, v.z));
    float lensq = vh.x * vh.x + vh.y * vh.y;
    vec3 t1 = lensq > 0.0 ? vec3(-vh.y, vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = cross(vh, t1);
    float r = sqrt(u.x);
    float phi = 6.2831853 * u.y;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
    vec3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
    return normalize(vec3(alpha * nh.x, alpha * nh.y, max(1.0e-6, nh.z)));
}
