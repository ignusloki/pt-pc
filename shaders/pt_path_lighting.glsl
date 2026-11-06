// Shared verbatim by the Original and Enhanced shader families.
#include "pt_foliage_shadow.glsl"

#include "pt_native_projection.glsl"

const uint kPtOccluderMask = 128u;
const uint kPtSurfaceMask = 8u;
const uint kPtLayerMask = 64u;
const float kPtPi = 3.14159265;

struct PtMaterial {
    vec3 albedo;
    float roughness;
    float specular;
    float material_u;
    float translucency;
    vec4 mat0;
    vec4 mat1;
};

void PtMaterialRows(inout PtMaterial s) {
    s.mat0 = Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(s.material_u, 0.25));
    s.mat1 = Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(s.material_u, 0.75));
}

bool PtLightReaches(uint i, vec3 p) {
    vec4 position = frame.lights[i].position;
    vec4 range = frame.lights[i].range;
    if (frame.lights[i].shadow_cone.w < 0.0) {
        return false;
    }
    float dist = length(position.xyz - p);
    float d = position.w > 0.5 ? max(range.y, dist + range.x) : max(range.y, dist) + range.x;
    float d2 = d * d;
    return d2 * d2 * range.z < 1.0;
}

bool PtLightEval(Light l, vec3 p, vec3 N, vec3 V, PtMaterial s, float cutoff, out vec3 diffuse, out vec3 specular) {
    diffuse = vec3(0.0);
    specular = vec3(0.0);
    if (l.info.z == 1) {
        vec3 q = (l.area * vec4(p, 1.0)).xyz;
        if (1.0 - max(abs(q.z), max(abs(q.x), abs(q.y))) < 0.0) {
            return false;
        }
    } else if (l.info.z == 2) {
        vec4 q = l.area * vec4(p, 1.0);
        if (q.w <= 0.0) {
            return false;
        }
        vec3 aperture = q.xyz / q.w;
        if (0.5 - max(abs(aperture.z), max(abs(aperture.x), abs(aperture.y))) < 0.0) {
            return false;
        }
    }
    bool spot = l.position.w > 0.5;
    vec3 lv = l.position.xyz - p;
    float dist = length(lv);
    vec3 L = lv / max(dist, 1.0e-6);
    float NdotL = dot(N, L);
    if (NdotL <= 0.0 && s.translucency <= 0.0) {
        return false;
    }
    float d = spot ? max(l.range.y, dist + l.range.x) : max(l.range.y, dist) + l.range.x;
    float d2 = d * d;
    vec3 radiance = l.color.rgb * max(0.0, 1.0 / d2 - d2 * l.range.z);
    if (spot) {
        float cos_a = dot(normalize(l.direction.xyz), -L);
        radiance *= pow(abs(Saturate((cos_a - l.cone.x) * l.cone.y)), l.cone.z);
    }
    if (length(radiance) <= cutoff) {
        return false;
    }
    vec3 mask = vec3(1.0);
    if (l.info.y >= 0 || l.info.w > 0) {
        vec4 c = l.mask * vec4(p, 1.0);
        if (c.w <= 0.0) {
            return false;
        }
        vec2 uv = vec2(0.5 * c.x / c.w + 0.5, 1.0 - (0.5 * c.y / c.w + 0.5));
        mask = l.info.w > 0 ? textureLod(PT_TEX2D(uint(l.info.w - 1)), uv, 0.0).rgb : ImgLod(uint(l.info.y), SMP_LINEAR_WRAP, uv, 0.0).rgb;
    }
    vec3 R = reflect(-V, N);
    vec3 to_ray = R - L;
    float k = Saturate(Saturate(0.9 * l.color.w / d) / max(length(to_ray), 1.0e-6));
    vec3 Ls = normalize(L + to_ray * k);
    vec3 H = normalize(Ls + V);
    float NdotH = Saturate(dot(N, H));
    float LdotH = dot(H, L);
    vec4 mat0 = s.mat0;
    vec4 mat1 = s.mat1;
    float w = s.translucency;
    float diff = Saturate(max(NdotL + w * (1.0 - NdotL * NdotL), w * mat0.w));
    diffuse = radiance * l.scales.y * diff * mask;
    vec3 spec_scale = radiance * l.scales.x * s.specular * mat0.rgb * mask;
    if (NdotL > 0.0 && dot(spec_scale, spec_scale) > 0.0) {
        vec3 lut_uv = vec3(NdotH * NdotH * 0.984375 + 0.0078125, s.roughness * s.roughness * 0.9375 + 0.03125, mat1.w * mat1.w * 0.9375 + 0.03125);
        vec2 ndf = texture(sampler3D(lut2_image, samplers[SMP_LINEAR_CLAMP]), lut_uv).xy;
        float D = ndf.x * ndf.x / max(ndf.y * ndf.y, 1.0e-8);
        float F = Img(RES_LUT1, SMP_LINEAR_CLAMP, vec2(LdotH, mat1.x)).x;
        specular = spec_scale * (Saturate(4.0 * NdotL) * F * D);
    }
    return true;
}

vec3 PtDiscPoint(Light l, vec3 p, vec2 u) {
    if (l.position.w < 0.5 && (uint(l.scales.w) & 1u) != 0u) {
        return l.position.xyz;
    }
    RtDisc d = RtLightDisc(l, p, vec2(0.0), 0.0);
    float r = d.radius * sqrt(u.x);
    float a = 6.2831853 * u.y;
    return l.position.xyz + (d.u * cos(a) + d.v * sin(a)) * r;
}

#include "rt_surface_uv.glsl"

bool PtCasterOpaque(uint record, uint primitive, vec2 bary, float u) {
    RtRecord r = rt_records[record];
    if ((r.flags & 3u) == 1u) {
        return true;
    }
    Material m = materials[r.material];
    float reference = (m.flags & MAT_ALPHA_DITHER) != 0u ? (1.0 + 126.0 * u) / 255.0 : 64.0 / 255.0;
    return textureLod(PT_TEX2D(m.albedo), PtCasterUV(record, primitive, bary), 0.0).a >= max(m.params.x, 0.0) * reference;
}

float PtCasterShadowTransmission(uint record, uint primitive, vec2 bary, Light l, float strength) {
    if (!(strength > 0.0)) {
        return 0.0;
    }
    RtRecord r = rt_records[record];
    Material m = materials[r.material];
    bool authored_shadow = (uint(l.scales.w) & 1u) != 0u;
    if (PtFoliageShadowTransmission(1.0, strength, m.flags, r.flags, authored_shadow) <= 0.0) {
        return 0.0;
    }
    float translucency = textureLod(PT_TEX2D(m.aux1), PtCasterUV(record, primitive, bary), 0.0).y;
    return PtFoliageShadowTransmission(translucency, strength, m.flags, r.flags, authored_shadow);
}

const float kPtFittingReach = kRtFittingReach;

bool PtProjectionClipsCaster(uint record, uint primitive, mat4x3 to_world, Light l) {
    RtRecord r = rt_records[record];
    RtIndexRef ib = RT_INDEX_REF(r.indices);
    RtVertexRef vb = RT_VERTEX_REF(r.vertices);
    uint base = r.first_index + 3u * primitive;
    uint i0 = (RT_INDEX(ib, base) + uint(r.vertex_offset)) * 22u;
    uint i1 = (RT_INDEX(ib, base + 1u) + uint(r.vertex_offset)) * 22u;
    uint i2 = (RT_INDEX(ib, base + 2u) + uint(r.vertex_offset)) * 22u;
    vec3 a = to_world * vec4(RT_VERTEX(vb, i0), RT_VERTEX(vb, i0 + 1u), RT_VERTEX(vb, i0 + 2u), 1.0);
    vec3 b = to_world * vec4(RT_VERTEX(vb, i1), RT_VERTEX(vb, i1 + 1u), RT_VERTEX(vb, i1 + 2u), 1.0);
    vec3 c = to_world * vec4(RT_VERTEX(vb, i2), RT_VERTEX(vb, i2 + 1u), RT_VERTEX(vb, i2 + 2u), 1.0);
    return PtProjectionClipsTriangle(l.position.xyz, normalize(l.direction.xyz), l.range.y, a, b, c);
}

float PtLightReach(Light l, vec3 origin, vec3 dir, float dist) {
    float t_max = dist - min(0.05, 0.5 * dist);
    if (l.position.w > 0.5) {
        vec3 axis = normalize(l.direction.xyz);
        float ahead = dot(origin - l.position.xyz, axis) - l.range.y;
        float rate = -dot(dir, axis);
        float t_plane = ahead > 0.0 ? (rate > 1.0e-6 ? ahead / rate : t_max) : 0.0;
        t_max = min(t_max, max(t_plane, dist - kPtFittingReach));
    }
    return t_max;
}

const float kPtSkinSelfReach = 0.3;
bool g_pt_origin_skinned = false;

float PtVisibility(Light l, vec3 origin, vec3 target, float foliage_strength, vec3 receiver_normal) {
    if ((uint(l.scales.w) & 4u) != 0u) {
        vec3 q = (l.mask * vec4(origin, 1.0)).xyz;
        if (max(abs(q.x), max(abs(q.y), abs(q.z))) <= 1.0) {
            vec3 n = normalize(transpose(inverse(mat3(l.mask))) * receiver_normal);
            if (n.y < -0.4 && abs(n.z) > 0.3 && abs(n.x) < 0.2) {
                return 1.0;
            }
        }
    }
    vec3 to_light = target - origin;
    float dist = length(to_light);
    if (dist <= 1.0e-4) {
        return 1.0;
    }
    vec3 dir = to_light / dist;
    float t_max = PtLightReach(l, origin, dir, dist);
    if (!RtRayValid(origin, dir, t_max)) {
        return 1.0;
    }
    float visibility = 1.0;
    rayQueryEXT rq;
    bool projection_clip = l.position.w > 0.5 && l.range.y > kPtFittingReach;
    uint flags = gl_RayFlagsTerminateOnFirstHitEXT | ((g_pt_origin_skinned || projection_clip) ? gl_RayFlagsNoOpaqueEXT : 0u);
    rayQueryInitializeEXT(rq, rt_casters, flags, RtFullCasters(l), origin, 0.0, dir, t_max);
    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) != gl_RayQueryCandidateIntersectionTriangleEXT) {
            continue;
        }
        uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
        if (g_pt_origin_skinned && (rt_records[record].flags & 4u) != 0u && rayQueryGetIntersectionTEXT(rq, false) < kPtSkinSelfReach) {
            continue;
        }
        vec3 hit = origin + dir * rayQueryGetIntersectionTEXT(rq, false);
        if (projection_clip && PtProjectionStaticCandidate(rt_records[record].flags, true, l.range.y, kPtFittingReach) &&
            distance(hit, l.position.xyz) < l.range.y &&
            PtProjectionClipsCaster(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, false),
                                    rayQueryGetIntersectionObjectToWorldEXT(rq, false), l)) {
            continue;
        }
        if ((rt_records[record].flags & 9u) == 0u) {
            rayQueryConfirmIntersectionEXT(rq);
            continue;
        }
        if (RtFittingPasses(record, l, hit)) {
            continue;
        }
        uint primitive = rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
        vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, false);
        if (PtCasterOpaque(record, primitive, bary, 0.5)) {
            float transmission = PtCasterShadowTransmission(record, primitive, bary, l, foliage_strength);
            if (transmission > 0.0) {
                visibility *= transmission;
                continue;
            }
            rayQueryConfirmIntersectionEXT(rq);
        }
    }
    return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT ? 0.0 : visibility;
}

bool PtOccluded(Light l, vec3 origin, vec3 target, vec3 receiver_normal, inout uint rng) {
    return PtVisibility(l, origin, target, 0.0, receiver_normal) <= 0.0;
}

bool g_pt_fill_unshadowed = false;

bool PtFillUnshadowed(uint light) {
    return g_pt_fill_unshadowed && rt_light_state[light].w < 0.5;
}

vec3 PtNextEventN(vec3 p, vec3 N, vec3 V, PtMaterial s, vec3 origin, uint light_count, float cutoff, uint samples, inout uint rng) {
    samples = clamp(samples, 1u, 4u);
    float u[4] = float[4](RtRandom(rng), RtRandom(rng), RtRandom(rng), RtRandom(rng));
    uint chosen[4] = uint[4](0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
    float chosen_weight[4] = float[4](0.0, 0.0, 0.0, 0.0);
    vec3 chosen_light[4] = vec3[4](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
    float total = 0.0;
    for (uint i = 0u; i < light_count; ++i) {
        if (!PtLightReaches(i, p)) {
            continue;
        }
        Light l = frame.lights[i];
        vec3 d;
        vec3 sp;
        if (!PtLightEval(l, p, N, V, s, cutoff, d, sp)) {
            continue;
        }
        vec3 c = s.albedo * d + sp;
        float w = Luma709(c);
        if (!(w > 0.0)) {
            continue;
        }
        total += w;
        float pick = w / total;
        for (uint k = 0u; k < samples; ++k) {
            if (u[k] < pick) {
                chosen[k] = i;
                chosen_weight[k] = w;
                chosen_light[k] = c;
                u[k] = u[k] / pick;
            } else {
                u[k] = (u[k] - pick) / max(1.0 - pick, 1.0e-6);
            }
        }
    }
    vec3 sum = vec3(0.0);
    for (uint k = 0u; k < samples; ++k) {
        if (chosen[k] == 0xFFFFFFFFu) {
            continue;
        }
        Light l = frame.lights[chosen[k]];
        vec3 target = PtDiscPoint(l, p, vec2(RtRandom(rng), RtRandom(rng)));
        if (!PtFillUnshadowed(chosen[k]) && PtOccluded(l, origin, target, N, rng)) {
            continue;
        }
        sum += chosen_light[k] * (total / chosen_weight[k]);
    }
    return sum / float(samples);
}

vec3 PtNextEvent(vec3 p, vec3 N, vec3 V, PtMaterial s, vec3 origin, uint light_count, float cutoff, inout uint rng) {
    return PtNextEventN(p, N, V, s, origin, light_count, cutoff, 1u, rng);
}

vec3 PtCosineDirection(vec3 n, vec2 u) {
    vec3 t = normalize(cross(n, abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(n, t);
    float phi = 6.2831853 * u.x;
    float sin_theta = sqrt(u.y);
    return normalize((t * cos(phi) + b * sin(phi)) * sin_theta + n * sqrt(max(0.0, 1.0 - u.y)));
}

float PtSpecularPower(float roughness, float floor_w) {
    return exp2(12.0 - 11.0 * max(roughness, floor_w));
}

bool PtSampleSpecular(vec3 N, vec3 V, PtMaterial s, vec2 u, out vec3 dir, out vec3 weight) {
    vec4 mat0 = s.mat0;
    vec4 mat1 = s.mat1;
    float n = PtSpecularPower(s.roughness, mat1.w);
    float cos_h = pow(max(u.x, 1.0e-7), 1.0 / (n + 1.0));
    float sin_h = sqrt(max(0.0, 1.0 - cos_h * cos_h));
    float phi = 6.2831853 * u.y;
    vec3 t = normalize(cross(N, abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(N, t);
    vec3 h = normalize(t * (cos(phi) * sin_h) + b * (sin(phi) * sin_h) + N * cos_h);
    float VdotH = dot(V, h);
    dir = reflect(-V, h);
    float NdotL = dot(N, dir);
    weight = vec3(0.0);
    if (VdotH <= 0.0 || NdotL <= 0.0) {
        return false;
    }
    float F = Img(RES_LUT1, SMP_LINEAR_CLAMP, vec2(VdotH, mat1.x)).x;
    weight = s.specular * mat0.rgb * (F * Saturate(4.0 * NdotL) * VdotH * (n + 2.04) / (n + 1.0));
    return true;
}

vec3 PtLobeDirection(vec3 N, vec3 V, float n, vec2 u) {
    float cos_h = pow(max(u.x, 1.0e-7), 1.0 / (n + 1.0));
    float sin_h = sqrt(max(0.0, 1.0 - cos_h * cos_h));
    float phi = 6.2831853 * u.y;
    vec3 t = normalize(cross(N, abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(N, t);
    vec3 h = normalize(t * (cos(phi) * sin_h) + b * (sin(phi) * sin_h) + N * cos_h);
    vec3 d = reflect(-V, h);
    return dot(d, N) > 0.0 ? d : reflect(-V, N);
}

float PtCoatAmount(vec3 n_world, vec3 face_world, vec3 n_view, vec3 up, float roughness, float scale, float bias) {
    if (dot(face_world, up) <= 0.7) {
        return 0.0;
    }
    float strength = Saturate(scale * pow(abs(1.0 - roughness), 4.0) * max(1.0, min(4.0, n_view.z * 3.0 + 2.0)) + bias);
    float a = strength * Saturate(dot(n_world, up));
    return a > 0.02 ? a : 0.0;
}

vec3 PtCoatNormal(vec3 n_world, vec3 face_world, vec3 V) {
    return normalize(mix(face_world, dot(n_world, V) > 0.0 ? n_world : face_world, 0.15));
}

vec3 PtCoatDirection(vec3 n_world, vec3 face_world, vec3 V) {
    return reflect(-V, PtCoatNormal(n_world, face_world, V));
}

vec3 PtSpecularAlbedo(vec3 N, vec3 V, PtMaterial s) {
    vec4 mat0 = s.mat0;
    vec4 mat1 = s.mat1;
    float F = Img(RES_LUT1, SMP_LINEAR_CLAMP, vec2(Saturate(dot(N, V)), mat1.x)).x;
    return s.specular * mat0.rgb * F;
}

PtMaterial PtHitMaterial(Material m, vec2 uv, float lod) {
    PtMaterial s;
    s.albedo = (m.flags & MAT_CONSTANT_COLOR) != 0u ? m.albedo_factor.rgb : textureLod(PT_TEX2D(m.albedo), uv, lod).rgb;
    vec4 srm = (m.flags & MAT_NORMAL_WAVE) != 0u ? vec4(m.extra.x, m.extra.y, 0.0, 0.0) : textureLod(PT_TEX2D(m.specular), uv, lod);
    s.specular = srm.r;
    s.roughness = srm.g;
    s.material_u = m.indices.x;
    s.translucency = 0.0;
    PtMaterialRows(s);
    return s;
}

bool g_pt_mirror_view = false;
