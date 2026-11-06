#version 460
#include "reflection_surface_helpers.glsl"

vec4 ShadeHit(uint record, uint primitive, vec2 bary, mat4x3 to_world, vec3 hit, vec3 ray) {
    RtRecord r = rt_records[record];
    RtIndexRef ib = RT_INDEX_REF(r.indices);
    RtVertexRef vb = RT_VERTEX_REF(r.vertices);
    uint base = r.first_index + 3u * primitive;
    uint a = (RT_INDEX(ib, base) + uint(r.vertex_offset)) * 22u;
    uint b = (RT_INDEX(ib, base + 1u) + uint(r.vertex_offset)) * 22u;
    uint c = (RT_INDEX(ib, base + 2u) + uint(r.vertex_offset)) * 22u;
    vec3 w = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
    vec2 uv = vec2(RT_VERTEX(vb, a + 10u), RT_VERTEX(vb, a + 11u)) * w.x + vec2(RT_VERTEX(vb, b + 10u), RT_VERTEX(vb, b + 11u)) * w.y + vec2(RT_VERTEX(vb, c + 10u), RT_VERTEX(vb, c + 11u)) * w.z;
    vec3 n_object = vec3(RT_VERTEX(vb, a + 3u), RT_VERTEX(vb, a + 4u), RT_VERTEX(vb, a + 5u)) * w.x + vec3(RT_VERTEX(vb, b + 3u), RT_VERTEX(vb, b + 4u), RT_VERTEX(vb, b + 5u)) * w.y +
                    vec3(RT_VERTEX(vb, c + 3u), RT_VERTEX(vb, c + 4u), RT_VERTEX(vb, c + 5u)) * w.z;
    vec3 n = normalize(mat3(to_world) * n_object);
    vec4 tangent_object = vec4(RT_VERTEX(vb, a + 6u), RT_VERTEX(vb, a + 7u), RT_VERTEX(vb, a + 8u), RT_VERTEX(vb, a + 9u)) * w.x +
                          vec4(RT_VERTEX(vb, b + 6u), RT_VERTEX(vb, b + 7u), RT_VERTEX(vb, b + 8u), RT_VERTEX(vb, b + 9u)) * w.y +
                          vec4(RT_VERTEX(vb, c + 6u), RT_VERTEX(vb, c + 7u), RT_VERTEX(vb, c + 8u), RT_VERTEX(vb, c + 9u)) * w.z;
    vec3 tangent = mat3(to_world) * tangent_object.xyz;
    tangent = dot(tangent, tangent) > 1.0e-12 ? normalize(tangent) : vec3(1.0, 0.0, 0.0);
    vec3 bitangent = tangent_object.w * normalize(cross(n, tangent) + vec3(0.0, 0.0, 1.0e-9));
    if (dot(n, ray) > 0.0) {
        n = -n;
    }
    Material m = materials[r.material];
    if (pass.ids.w == 7u) {
        return vec4(float(m.kind == 0u), float(m.kind == 2u), float(m.kind == 1u || m.kind == 3u || m.kind == 4u), 1.0);
    }
    if (m.kind == KIND_CONSTANT || m.kind == KIND_SKY) {
        vec4 t = textureLod(PT_TEX2D(m.albedo), uv, 2.0);
        return vec4(min(vec3(32.0), m.albedo_factor.rgb * t.rgb * m.params.y * v.exposure.x) * Saturate(m.albedo_factor.a * t.a), 1.0);
    }
    if (m.kind != KIND_DEFERRED) {
        return vec4(0.0);
    }
    if ((m.flags & MAT_NORMAL_MAP) != 0u) {
        vec4 nt = textureLod(PT_TEX2D(m.normal), uv, 2.0);
        vec2 nxy = vec2(nt.w, nt.y) * 2.0 - 1.0;
        float nz = sqrt(Saturate(1.0 - dot(nxy, nxy)) + 1.00016594e-4);
        vec3 tangent_normal = vec3(nxy, nz);
        if ((m.flags & MAT_SUB_NORMAL) != 0u) {
            vec4 mask = textureLod(PT_TEX2D(m.aux2), uv, 2.0);
            vec4 detail = textureLod(PT_TEX2D(m.aux1), uv * m.extra.yz, 2.0);
            float k = m.extra.x * mask.x;
            tangent_normal = normalize(vec3(nxy.x + k * (2.0 * detail.w - 1.0), nxy.y + k * (2.0 * detail.y - 1.0), nz));
        }
        n = RtMappedNormal(n, tangent, bitangent, tangent_normal);
    }
    vec3 albedo = (m.flags & MAT_CONSTANT_COLOR) != 0u ? m.albedo_factor.rgb : textureLod(PT_TEX2D(m.albedo), uv, 2.0).rgb;

    vec3 ambient = vec3(0.0);
    vec3 probe_n = vec3(-n.x, -n.z, n.y);
    for (uint i = 0u; i < pass.ids.z; ++i) {
        Probe p = frame.probes[i];
        vec3 q = (p.box * vec4(hit, 1.0)).xyz;
        vec3 f = min(clamp((1.0 - q) * p.positive.xyz, 0.0, 1.0), clamp((1.0 + q) * p.negative.xyz, 0.0, 1.0));
        float weight = p.positive.w * f.x * f.y * f.z;
        if (weight > 0.0) {
            ambient = max(vec3(0.0), weight * EvaluateSh(p, probe_n) * v.exposure.x) + ambient * max(0.0, 1.0 - weight);
        }
    }

    vec3 direct = vec3(0.0);
    for (uint i = 0u; i < pass.ids.y; ++i) {
        if (LightHidden(i)) {
            continue;
        }
        Light l = frame.lights[i];
        if (l.info.z == 1) {
            vec3 q = (l.area * vec4(hit, 1.0)).xyz;
            if (1.0 - max(abs(q.z), max(abs(q.x), abs(q.y))) < 0.0) {
                continue;
            }
        } else if (l.info.z == 2) {
            vec4 q = l.area * vec4(hit, 1.0);
            if (q.w <= 0.0) {
                continue;
            }
            vec3 aperture = q.xyz / q.w;
            if (0.5 - max(abs(aperture.z), max(abs(aperture.x), abs(aperture.y))) < 0.0) {
                continue;
            }
        }
        bool spot = l.position.w > 0.5;
        vec3 lv = l.position.xyz - hit;
        float dist = length(lv);
        vec3 L = lv / max(dist, 1.0e-6);
        float ndl = dot(n, L);
        if (ndl <= 0.0) {
            continue;
        }
        float d = spot ? max(l.range.y, dist + l.range.x) : max(l.range.y, dist) + l.range.x;
        float d2 = d * d;
        vec3 radiance = l.color.rgb * max(0.0, 1.0 / d2 - d2 * l.range.z) * v.exposure.x;
        float shadow_cone = 1.0;
        if (spot) {
            float cos_a = dot(normalize(l.direction.xyz), -L);
            radiance *= pow(abs(Saturate((cos_a - l.cone.x) * l.cone.y)), l.cone.z);
            float sc = Saturate((cos_a - l.shadow_cone.x) * l.shadow_cone.y);
            shadow_cone = sc * sc;
        }
        if (length(radiance) <= 1.0e-4) {
            continue;
        }
        vec3 mask = vec3(1.0);
        if (l.info.y >= 0 || l.info.w > 0) {
            vec4 mc = l.mask * vec4(hit, 1.0);
            if (mc.w <= 0.0) {
                continue;
            }
            vec2 muv = vec2(0.5 * mc.x / mc.w + 0.5, 1.0 - (0.5 * mc.y / mc.w + 0.5));
            mask = l.info.w > 0 ? textureLod(PT_TEX2D(uint(l.info.w - 1)), muv, 0.0).rgb : ImgLod(uint(l.info.y), SMP_LINEAR_WRAP, muv, 0.0).rgb;
        }
        float vis = 1.0;
        if (l.info.x >= 0 && RtOccluded(l, hit, l.position.xyz, spot, 1u | (0x10u << 8u))) {
            float s = 1.0 - shadow_cone;
            vis = 1.0 + l.scales.z * (s * s - 1.0);
        }
        direct += radiance * l.scales.y * ndl * vis * mask;
    }
    if (pass.ids.w >= 3u) {
        vec3 d = pass.ids.w == 3u ? albedo : pass.ids.w == 4u ? ambient : pass.ids.w == 5u ? direct : pass.ids.w == 6u ? n * 0.5 + 0.5
               : vec3(float(m.kind) / 8.0, float(m.flags & 0xFFu) / 255.0, float((m.flags >> 8u) & 0xFFu) / 255.0);
        return vec4(d, 1.0);
    }
    return vec4(albedo * (ambient + direct), 1.0);
}

#include "rt_surface_uv.glsl"

vec4 TracedLayers(vec3 origin, vec3 ray, float t_end) {
    vec3 sum = vec3(0.0);
    float seen = 1.0;
    float t_start = 0.0;
    for (int k = 0; k < 4 && seen > 0.004; ++k) {
        rayQueryEXT rq;
        rayQueryInitializeEXT(rq, rt_casters, gl_RayFlagsCullBackFacingTrianglesEXT, 64u, origin, t_start, ray, t_end);
        while (rayQueryProceedEXT(rq)) {
            if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
                rayQueryConfirmIntersectionEXT(rq);
            }
        }
        if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
            break;
        }
        uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
        Material m = materials[rt_records[record].material];
        if (m.kind == KIND_CONSTANT) {
            vec2 uv = PtCasterUV(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, true), rayQueryGetIntersectionBarycentricsEXT(rq, true));
            vec4 t = textureLod(PT_TEX2D(m.albedo), uv, 2.0);
            vec3 c = min(vec3(32.0), m.albedo_factor.rgb * t.rgb * m.params.y * v.exposure.x);
            float a = Saturate(m.albedo_factor.a * t.a);
            sum += seen * c * a;
            seen *= 1.0 - a;
        }
        t_start = rayQueryGetIntersectionTEXT(rq, true) + 1.0e-3;
        if (!(t_start < t_end)) {
            break;
        }
    }
    return vec4(sum, seen);
}

#include "reflection_rt_resolve.glsl"
