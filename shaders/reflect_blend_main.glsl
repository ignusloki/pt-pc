#include "reflect_blend_main_rt_tap_shared.glsl"

void main() {
    View v = frame.views[pass.ids.x];
#ifdef PT_REFLECT_LAYER
    g_reflection_depth_legacy = pass.f0.w > 0.5;
#endif
    vec2 uv = (gl_FragCoord.xy - 0.00390625) * v.viewport.zw;
    vec2 map_scale = pass.f1.xy;
    vec4 base = ImgLod(IMG_HDR_COPY, SMP_LINEAR_WRAP, uv, 0.0);
    vec3 N = DecodeNormal(ImgLod(IMG_NORMAL, SMP_POINT_CLAMP, uv, 0.0).xyz);
    vec4 material = ImgLod(IMG_MATERIAL, SMP_LINEAR_WRAP, uv, 0.0);
    vec4 center = ImgLod(IMG_REFMAP, SMP_LINEAR_WRAP, uv / map_scale, 0.0);
    vec3 plane = pass.f0.xyz;
    float weight = center.z * center.w;
#ifdef PT_RT_REFLECTIONS
    weight = RtReflectionCenterWeight(center, textureLod(rt_reflection_color, uv / map_scale, 0.0));
#endif
    vec2 tilt = (N.xy - plane.xy) * weight;
    vec4 refl = ImgLod(IMG_REFMAP, SMP_LINEAR_WRAP, (uv + 0.15 * vec2(-tilt.x, tilt.y)) / map_scale, 0.0);
    float valid = refl.y < 0.0 || refl.y > 1.0 || refl.x > 1.0 || refl.x < 0.0 ? 0.0 : refl.w;
#ifdef PT_RT_REFLECTIONS
    vec2 lookup = uv + 0.15 * vec2(-tilt.x, tilt.y);
#ifdef PT_REFLECT_LAYER
    float turn_frame = floor(v.exposure.w * 60.0 + 0.5);
#else
    float turn_frame = 0.0;
#endif
    vec4 contribution = RtGlossyContribution(v, uv, lookup, map_scale, material.x, refl, turn_frame, plane, pass.ids.z != 0u);
    if (!(contribution.a > 1.0e-4)) {
#ifdef PT_REFLECT_LAYER
        out_color = NoReflection(N, plane);
        out_history = HistoryOffset(v, uv, uv, false);
#else
        out_color = base;
#endif
        return;
    }
#else
    if (!(valid > 0.0)) {
#ifdef PT_REFLECT_LAYER
        out_color = NoReflection(N, plane);
        out_history = HistoryOffset(v, uv, uv, false);
#else
        out_color = base;
#endif
        return;
    }
#endif
    float facing = Saturate(dot(N, plane));
    float lean = dot(N, pass.f2.xyz);
    float smooth_term = pow(abs(1.0 - material.x), 4.0);
    float strength = Saturate(pass.f1.z * smooth_term * max(1.0, min(4.0, lean * 3.0 + 2.0)) + pass.f1.w);
#ifdef PT_RT_REFLECTIONS
    vec2 coordinate = RtScreenCoordinate(refl);
    float amount = strength * facing * contribution.a;
#else
    float amount = strength * facing * refl.z * valid;
#endif
    if (pass.f2.w < 1.0) {
        amount -= abs(2.0 * uv.x - 1.0) * amount;
    }
    vec3 hit = ImgLod(IMG_HDR_COPY, SMP_LINEAR_WRAP, uv + map_scale * (2.0 * refl.xy - 1.0), 0.0).rgb;
#ifdef PT_RT_REFLECTIONS
    hit = contribution.rgb;
#endif
#ifdef PT_REFLECT_LAYER
    amount = max(amount, 0.0);
    out_color = vec4(SrgbEncode(min(hit, vec3(1.0))) * amount, amount);
#ifdef PT_RT_REFLECTIONS
    bool on_screen = refl.w > 1.0e-4 && refl.z > 0.0 && all(greaterThanEqual(coordinate, vec2(0.0))) && all(lessThanEqual(coordinate, vec2(1.0)));
    out_history = HistoryOffset(v, uv, uv + map_scale * (2.0 * coordinate - 1.0), on_screen);
#else
    out_history = HistoryOffset(v, uv, uv + map_scale * (2.0 * refl.xy - 1.0), true);
#endif
#else
    out_color = vec4(SrgbDecode(mix(ReflectionBlendBase(base.rgb, pass.ids.y != 0u), SrgbEncode(min(hit, vec3(1.0))), amount)), base.a);
#endif
}
