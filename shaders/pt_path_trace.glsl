// Shared verbatim by the Original and Enhanced shader families.
uint g_pt_first_mask = kPtSurfaceMask;
bool g_pt_seen_emissive = false;
float PtEmissiveCap(uint bounce, float cutoff) {
    return bounce == 0u && g_pt_seen_emissive ? 32.0 * cutoff * 1.0e4 : 32.0;
}
uint g_pt_first_lights = 1u;
bool g_pt_first_through_unlit = false;

vec3 PtTracePath(vec3 origin, vec3 dir, vec3 throughput, uint bounces, float reach, float lod, uint light_count, float cutoff, inout uint rng,
                 out float first_distance) {
    vec3 radiance = vec3(0.0);
    first_distance = reach;
    uint through = 0u;
    for (uint bounce = 0u; bounce < bounces; ++bounce) {
        if (!RtRayValid(origin, dir, reach)) {
            break;
        }
        rayQueryEXT rq;
        uint flags = g_pt_mirror_view ? gl_RayFlagsNoOpaqueEXT : gl_RayFlagsNoneEXT;
        rayQueryInitializeEXT(rq, rt_casters, flags, bounce == 0u ? g_pt_first_mask : kPtSurfaceMask, origin, 0.0, dir, reach);
        while (rayQueryProceedEXT(rq)) {
            if (rayQueryGetIntersectionTypeEXT(rq, false) != gl_RayQueryCandidateIntersectionTriangleEXT) {
                continue;
            }
            uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
            uint record_flags = rt_records[record].flags;
            if (g_pt_mirror_view && (record_flags & 1024u) != 0u) {
                continue;
            }
            if ((g_pt_mirror_view && (record_flags & 9u) == 0u) ||
                PtCasterOpaque(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, false),
                               rayQueryGetIntersectionBarycentricsEXT(rq, false), RtRandom(rng))) {
                rayQueryConfirmIntersectionEXT(rq);
            }
        }
        bool hit = rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT;
        float t = hit ? rayQueryGetIntersectionTEXT(rq, true) : reach;
        if (bounce == 0u) {
            first_distance = t;
        }
#if defined(PT_D3D12) && defined(PT_INDIRECT_RENDER_SIZE)
        uint record = 0u;
        RtSurfaceHit hs = RtSurfaceHit(vec3(0.0), vec2(0.0), 0u);
        bool hit_front = false;
        if (hit) {
            record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
            hs = RtFullSurface(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, true), rayQueryGetIntersectionBarycentricsEXT(rq, true),
                               rayQueryGetIntersectionObjectToWorldEXT(rq, true), dir);
            hit_front = rayQueryGetIntersectionFrontFaceEXT(rq, true);
        }
#endif
        vec3 glass_point;
        vec3 glass_normal;
        if (PtLayers(origin, dir, hit ? t + max(1.0e-3, 2.0e-4 * t) : t, PtEmissiveCap(bounce, cutoff), radiance, throughput, rng, glass_point, glass_normal)) {
            origin = glass_point + glass_normal * 0.002;
            dir = reflect(dir, glass_normal);
            continue;
        }
        if (!hit) {
            break;
        }
#if !defined(PT_D3D12) || !defined(PT_INDIRECT_RENDER_SIZE)
        uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
        RtSurfaceHit hs = RtFullSurface(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, true), rayQueryGetIntersectionBarycentricsEXT(rq, true),
                                        rayQueryGetIntersectionObjectToWorldEXT(rq, true), dir);
#endif
        Material m = materials[hs.material];
#if defined(PT_D3D12) && defined(PT_INDIRECT_RENDER_SIZE)
        bool front = hit_front || (m.flags & MAT_TWO_SIDED) != 0u;
#else
        bool front = rayQueryGetIntersectionFrontFaceEXT(rq, true) || (m.flags & MAT_TWO_SIDED) != 0u;
#endif
        bool mirror_lining = g_pt_mirror_view && bounce == 0u && (rt_records[record].flags & 4u) != 0u &&
                             (m.flags & MAT_MIRROR_LINING) != 0u;
        front = front || mirror_lining;
        if (g_pt_first_through_unlit && bounce == 0u && m.kind != KIND_DEFERRED && through < 4u) {
            origin = origin + dir * (t + max(1.0e-3, 2.0e-4 * t));
            ++through;
            --bounce;
            continue;
        }
        if (!front || m.kind != KIND_DEFERRED) {
            break;
        }
        PtMaterial s = PtHitMaterial(m, hs.uv, lod);
        if (mirror_lining) {
            s.albedo = mix(s.albedo, vec3(1.0), 0.035);
        }
        vec3 p = origin + dir * t;
        vec3 N = hs.normal;
        vec3 V = -dir;
        vec3 next = p + N * 0.002;
        uint lights = bounce == 0u ? max(g_pt_first_lights, 1u) : 1u;
        g_pt_origin_skinned = (rt_records[record].flags & 4u) != 0u;
        radiance += throughput * PtNextEventN(p, N, V, s, next, light_count, cutoff, lights, rng);
        g_pt_origin_skinned = false;
        if (bounce + 1u >= bounces) {
            break;
        }
        vec3 spec_albedo = PtSpecularAlbedo(N, V, s);
        float ls = Luma709(spec_albedo);
        float ld = Luma709(s.albedo);
        if (!(ls + ld > 1.0e-5)) {
            break;
        }
        float p_spec = clamp(ls / (ls + ld), 0.0, 1.0);
        if (RtRandom(rng) < p_spec) {
            vec3 weight;
            if (!PtSampleSpecular(N, V, s, vec2(RtRandom(rng), RtRandom(rng)), dir, weight)) {
                break;
            }
            throughput *= weight / max(p_spec, 1.0e-4);
        } else {
            dir = PtCosineDirection(N, vec2(RtRandom(rng), RtRandom(rng)));
            throughput *= s.albedo / max(1.0 - p_spec, 1.0e-4);
        }
        origin = next;
        if (bounce >= 1u) {
            float q = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
            if (RtRandom(rng) >= q) {
                break;
            }
            throughput /= q;
        }
    }
    return radiance;
}
