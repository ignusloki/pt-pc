// Shared verbatim by the Original and Enhanced shader families.
vec4 ScreenMarch(vec2 uv, vec3 P, vec3 R, vec2 start, vec2 stride, float scale, float inv_z, float inv_z_step, float far_limit) {
    float offset = 0.0;
    if (pass.f2.z > 0.5) {
        vec2 noise_pixel = gl_FragCoord.xy + 5.588238 * pass.f2.w;
        offset = fract(52.9829189 * fract(dot(noise_pixel, vec2(0.06711056, 0.00583715))));
    }
    bool terminated = false;
    bool behind_previous = false;
    int hit = -1;
    for (int k = 0; k < 16; ++k) {
        float s = k == 0 && pass.f1.w < 0.5 ? 1.0 : 1.0 + 25.0 * (float(k) + offset);
        vec2 suv = 0.5 * (start + s * stride) + 0.5;
        float sz = SceneZ(suv);
        terminated = terminated || behind_previous || far_limit < sz || Outside(suv);
        bool behind = 1.0 / (inv_z_step * s + inv_z) > sz;
        if (k > 0 && behind && !terminated && hit < 0) {
            hit = k - 1;
        }
        behind_previous = behind;
    }
    if (hit < 0) {
        return vec4(0.0);
    }
    float lo = hit == 0 && pass.f1.w < 0.5 ? 1.0 : 1.0 + 25.0 * (float(hit) + offset);
    float hi = 1.0 + 25.0 * (float(hit + 1) + offset);
    float best_error = 999.0;
    float best_step = 0.0;
    vec2 best_uv = vec2(0.0);
    bool invalid = false;
    for (int i = 0; i < 6; ++i) {
        float mid = (hi - lo) * 0.5 + lo;
        vec2 muv = 0.5 * (start + mid * stride) + 0.5;
        float mz = SceneZ(muv);
        float ray_z = 1.0 / (inv_z_step * mid + inv_z);
        invalid = invalid || Outside(muv) || far_limit < mz;
        float error = abs(ray_z - mz);
        if (best_error > error) {
            best_error = error;
            best_step = mid;
            best_uv = muv;
        }
        if (ray_z > mz) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    if (invalid) {
        return vec4(0.0);
    }
    float travel = best_step * scale * length(R);
    float screen_fade = Saturate(best_step * 0.0026595744);
    screen_fade = 1.0 - screen_fade * screen_fade;
    float ray_fade = 1.0 - Saturate(travel * 0.02);
    float path_fade = 1.0 - Saturate((sqrt(P.z * P.z + 1.0 + P.y * P.y + P.x * P.x) + travel) * 0.002);
    float confidence = min(path_fade, min(screen_fade, ray_fade)) * (1.0 - Saturate(best_error * 10.0));
    return vec4(clamp(0.5 * (best_uv - uv) + 0.5, 0.0, 1.0), confidence, 1.0);
}

float MarchConfidence(vec3 P, vec3 X, vec2 start, vec2 stride, float scale) {
    if (!(X.z > v.projection_param.z)) {
        return 0.0;
    }
    float s = max(1.0, dot(Project(X) - start, stride) / max(dot(stride, stride), 1.0e-20));
    float travel = s * scale;
    float screen_fade = Saturate(s * 0.0026595744);
    screen_fade = 1.0 - screen_fade * screen_fade;
    float ray_fade = 1.0 - Saturate(travel * 0.02);
    float path_fade = 1.0 - Saturate((sqrt(P.z * P.z + 1.0 + P.y * P.y + P.x * P.x) + travel) * 0.002);
    return min(path_fade, min(screen_fade, ray_fade));
}

bool FullFade() {
    return pass.m[3][3] > 0.5;
}

float DistanceFade(float travel) {
    return 1.0 - smoothstep(12.0, 30.0, travel);
}

void main() {
    v = frame.views[pass.ids.x];
    g_reflection_depth_legacy = pass.f1.z > 0.5;
    vec3 plane = pass.f0.xyz;
    float far_limit = pass.f0.w;
    vec2 uv = (gl_FragCoord.xy - 0.00390625) * pass.f2.xy;
    out_color = vec4(0.0);
    vec3 N = DecodeNormal(ImgLod(IMG_NORMAL, SMP_POINT_CLAMP, uv, 0.0).xyz);
    if (!(abs(dot(plane, N)) >= 0.9)) {
        out_map = vec4(0.0, 0.0, 1.0, 0.0);
        return;
    }
    float z = SceneZ(uv);
    if (far_limit < z) {
        out_map = vec4(0.0, 0.0, 1.0, 0.0);
        return;
    }
    vec2 ndc = in_uv * 2.0 - 1.0;
    vec3 P = ReflectionViewPosition(ndc, v.projection_param.xy, v.jitter.xy, z);
    vec3 D = P * inversesqrt(dot(P, P));
    vec3 R = D - 2.0 * dot(plane, D) * plane;
    vec3 P2 = P + R;
    vec2 start = Project(P);
    vec2 delta = Project(P2) - start;
    float scale = 1.0 / length(0.4 * pass.f1.xy * delta);
    vec2 stride = delta * scale;
    float inv_z = 1.0 / P.z;
    float inv_z_step = (1.0 / P2.z - inv_z) * scale;
    vec4 march = ScreenMarch(uv, P, R, start, stride, scale, inv_z, inv_z_step, far_limit);
    out_map = march;

    vec3 up = mat3(v.inv_view) * plane;
    vec3 origin = (v.inv_view * vec4(P, 1.0)).xyz + up * 0.005;
    vec3 ray = normalize(mat3(v.inv_view) * R);
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, rt_casters, gl_RayFlagsCullBackFacingTrianglesEXT, 4u, origin, 0.0, ray, kReach);
    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT &&
            RtCasterOpaque(rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false), rayQueryGetIntersectionPrimitiveIndexEXT(rq, false),
                           rayQueryGetIntersectionBarycentricsEXT(rq, false))) {
            rayQueryConfirmIntersectionEXT(rq);
        }
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
        if (pass.ids.w == 1u) {
            out_color = vec4(1.0, 1.0, 0.0, 1.0);
        }
        return;
    }
    float travel = rayQueryGetIntersectionTEXT(rq, true);
    vec3 hit = origin + ray * travel;
    vec3 H = (v.view * vec4(hit, 1.0)).xyz;
    if (H.z > v.projection_param.z && pass.ids.w < 2u) {
        vec2 hit_uv = 0.5 * Project(H) + 0.5;
        vec3 X = H;
        bool unresolved = false;
        if (!Outside(hit_uv) && SceneZ(hit_uv) < H.z * 0.98 - 0.02) {
            const int kSteps = 48;
            float lo = 0.0;
            float hi = -1.0;
            for (int k = 1; k <= kSteps; ++k) {
                float t = float(k) / float(kSteps);
                vec3 Q = mix(P, H, t);
                vec2 quv = 0.5 * Project(Q) + 0.5;
                if (Outside(quv) || Q.z <= v.projection_param.z) {
                    break;
                }
                if (Q.z > SceneZ(quv)) {
                    hi = t;
                    break;
                }
                lo = t;
            }
            if (hi > 0.0) {
                for (int i = 0; i < 6; ++i) {
                    float mid = 0.5 * (lo + hi);
                    vec3 Q = mix(P, H, mid);
                    if (Q.z > SceneZ(0.5 * Project(Q) + 0.5)) {
                        hi = mid;
                    } else {
                        lo = mid;
                    }
                }
                X = mix(P, H, hi);
                hit_uv = 0.5 * Project(X) + 0.5;
            } else {
                unresolved = true;
            }
        }
        if (!unresolved && !Outside(hit_uv)) {
            float confidence = FullFade() ? DistanceFade(travel) : MarchConfidence(P, X, start, stride, scale);
            out_map = vec4(clamp(0.5 * (hit_uv - uv) + 0.5, 0.0, 1.0), confidence, 1.0);
            if (pass.ids.w == 1u) {
                out_map = march;
                out_color = vec4(0.0, confidence, 0.0, confidence);
            }
            return;
        }
    }
    float weight = pass.ids.w >= 2u ? 1.0 : FullFade() ? DistanceFade(travel) : MarchConfidence(P, H, start, stride, scale);
    if (!(weight > 0.0)) {
        return;
    }
    vec4 color = ShadeHit(rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true), rayQueryGetIntersectionPrimitiveIndexEXT(rq, true),
                          rayQueryGetIntersectionBarycentricsEXT(rq, true), rayQueryGetIntersectionObjectToWorldEXT(rq, true), hit, ray);
    if (pass.ids.w < 3u) {
        vec4 layers = TracedLayers(origin, ray, travel);
        if (color.w > 0.0) {
            color = vec4(layers.rgb + layers.a * color.rgb, 1.0);
        } else if (layers.a < 1.0) {
            color = vec4(layers.rgb / (1.0 - layers.a), 1.0 - layers.a);
        }
    }
    weight *= color.w;
    out_color = vec4(color.rgb * weight, weight);
    out_map = vec4(march.xy, march.z * (1.0 - weight), march.w);
    if (pass.ids.w == 1u) {
        out_map = march;
        out_color = vec4(weight, 0.0, 0.0, weight);
    }
}
