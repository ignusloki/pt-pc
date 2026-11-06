// Shared verbatim by the Original and Enhanced shader families.
#include "common.glsl"
#include "reflection_blend.glsl"
#ifdef PT_RT_REFLECTIONS
layout(set = 2, binding = 2) uniform sampler2D rt_reflection_color;
#include "reflection_mix.glsl"
#endif
#ifdef PT_RT_REFLECTIONS
const int kGlossTaps = 12;
const float kGlossStretch = 2.5;

vec4 RtTap(vec2 base, vec2 tap_uv, vec2 map_scale) {
    vec4 screen_map = ImgLod(IMG_REFMAP, SMP_LINEAR_WRAP, tap_uv / map_scale, 0.0);
    vec4 traced = textureLod(rt_reflection_color, tap_uv / map_scale, 0.0);
    vec2 coordinate = RtScreenCoordinate(screen_map);
    vec3 screen_hit = ImgLod(IMG_HDR_COPY, SMP_LINEAR_WRAP, base + map_scale * (2.0 * coordinate - 1.0), 0.0).rgb;
    return RtReflectionContribution(screen_map, traced, screen_hit);
}

vec4 RtGlossyContribution(View v, vec2 uv, vec2 lookup, vec2 map_scale, float roughness, vec4 center_map, float turn_frame, vec3 plane, bool full) {
    vec2 coordinate = RtScreenCoordinate(center_map);
    bool on_screen = center_map.w > 1.0e-4 && center_map.z > 0.0;
    float length_on_screen = on_screen ? length(map_scale * (2.0 * coordinate - 1.0)) : 0.4;
    float spread = (0.003 + 0.02 * Saturate(roughness)) * clamp(length_on_screen / 0.25, 0.25, 1.2);
    vec2 radius = vec2(spread * 0.4 * v.viewport.x * v.viewport.w, spread);
    if (full) {
        vec3 view_ray = normalize(vec3((uv * 2.0 - 1.0) * v.projection_param.xy, 1.0));
        float stretch = min(1.0 / max(abs(dot(view_ray, plane)), 1.0e-3), kGlossStretch);
        radius = vec2(spread * v.viewport.y * v.viewport.z, spread * stretch);
    }
    float turn = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy + 5.588238 * mod(turn_frame, 64.0), vec2(0.06711056, 0.00583715))));
    vec3 sum = vec3(0.0);
    float covered = 0.0;
    for (int i = 0; i < kGlossTaps; ++i) {
        float r = sqrt((float(i) + 0.5) / float(kGlossTaps));
        float a = turn + float(i) * 2.39996323;
        vec2 offset = radius * r * vec2(cos(a), sin(a));
        vec4 c = full ? RtTap(uv + offset, lookup + offset, map_scale) : RtTap(uv, lookup + offset, map_scale);
        sum += c.rgb * c.a;
        covered += c.a;
    }
    if (!(covered > 1.0e-4)) {
        return vec4(0.0);
    }
    return vec4(sum / covered, covered / float(kGlossTaps));
}
#endif
#ifdef PT_REFLECT_LAYER
#ifndef PT_RT_REFLECTIONS
#include "reflection_mix.glsl"
#endif
#include "reflection_depth.glsl"
#endif

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
#ifdef PT_REFLECT_LAYER
layout(location = 1) out vec4 out_history;

vec4 NoReflection(vec3 N, vec3 plane) {
    return vec4(0.0, 0.0, 0.0, dot(N, plane) >= 0.9 ? 0.0 : -1.0);
}

vec4 HistoryOffset(View v, vec2 uv, vec2 hit_uv, bool has_hit) {
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 P = ReflectionViewPosition(ndc, v.projection_param.xy, v.jitter.xy, ViewZ(v, ReflectionFloorDepth(v, uv)));
    float distance = length(P);
    float hit_depth = has_hit ? ReflectionFloorDepth(v, hit_uv) : 0.0;
    has_hit = hit_depth > 0.0;
    if (has_hit) {
        vec3 H = ReflectionViewPosition(hit_uv * 2.0 - 1.0, v.projection_param.xy, v.jitter.xy, ViewZ(v, hit_depth));
        distance += length(H - P);
    }
    vec3 virtual_image = P * (distance / max(length(P), 1.0e-6));
    vec4 previous = pass.m * vec4((v.inv_view * vec4(virtual_image, 1.0)).xyz, 1.0);
    if (!(previous.w > 1.0e-6)) {
        return vec4(0.0, 0.0, -1.0, 0.0);
    }
    return vec4((previous.xy / previous.w - (ndc - v.jitter.xy)) * 0.5, has_hit ? 1.0 : 0.0, 0.0);
}
#endif
