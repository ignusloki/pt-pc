#include "common.glsl"
#define PT_LIGHT_GBUFFER_LATE 1
#if defined(PT_RT_SHADOWS) || defined(PT_RT_CONTACT) || defined(PT_RT_FULL)
#include "rt_shadow.glsl"
#endif
#ifdef PT_RT_FULL
#include "rt_full.glsl"
#endif
#include "lighting.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) out vec4 out_diffuse;
layout(location = 1) out vec4 out_specular;
#ifdef PT_RT_FULL
layout(location = 2) out vec4 out_shadowed;
#endif

void main() {
    View v = frame.views[pass.ids.x];
    Light l = frame.lights[pass.ids.y];
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = ImgFetch(IMG_DEPTH, pixel).x;
    if (depth <= 0.0) {
        discard;
    }
    Surface s;
    s.P = ViewPosition(v, PixelNdc(v, gl_FragCoord.xy), depth);
    s.world = (v.inv_view * vec4(s.P, 1.0)).xyz;
    vec3 diffuse;
    vec3 specular;
#if defined(PT_RT_SHADOWS) || defined(PT_RT_CONTACT)
    g_contact_reach = pass.f0.x;
    g_contact_legacy = pass.f0.y > 0.5;
    g_contact_samples = uint(pass.f0.z + 0.5);
#endif
    if (!EvaluateLight(l, v, s, pass.ids.z != 0u, gl_FragCoord.xy, diffuse, specular)) {
        discard;
    }
#ifdef PT_RT_FULL
    float vis = 1.0;
    float strength = pass.ids.z != 0u && l.info.x >= 0 ? l.shadow_cone.z * rt_light_state[pass.ids.y].w : 0.0;
    float amount = Luma709(diffuse) + Luma709(specular);
    if (strength > 0.001 && g_full_lit && amount > pass.f0.w) {
        uint samples = (uint(l.info.x) >> 16u) & 0xFFu;
        float offset = max(0.003, 0.001 * s.P.z);
        vec3 ng = normalize(mat3(v.inv_view) * RtFaceNormal(v, pixel, ivec2(v.viewport.xy), s.P));
        float raw = RtFullShadow(l, s.world, ng, offset, g_full_n, g_full_shadow_cone, samples, gl_FragCoord.xy, v.exposure.w);
        vis = 1.0 + strength * (raw - 1.0);
    }
    float spec = Luma709(specular);
    out_diffuse = vec4(diffuse, spec);
    out_specular = vec4(specular, 1.0);
    out_shadowed = vec4(diffuse * vis, spec * vis);
#else
    out_diffuse = vec4(diffuse, 1.0);
    out_specular = vec4(specular, 1.0);
#endif
}
