#include "pt_path_lighting.glsl"

bool PtLayers(vec3 origin, vec3 dir, float t_end, float emissive_cap, inout vec3 radiance, inout vec3 throughput, inout uint rng, out vec3 glass_point,
            out vec3 glass_normal) {
    float t_start = 0.0;
    glass_point = vec3(0.0);
    glass_normal = vec3(0.0);
    for (int k = 0; k < 3; ++k) {
        rayQueryEXT rq;
        uint flags = gl_RayFlagsCullBackFacingTrianglesEXT | (g_pt_mirror_view ? gl_RayFlagsNoOpaqueEXT : 0u);
        rayQueryInitializeEXT(rq, rt_casters, flags, kPtLayerMask, origin, t_start, dir, t_end);
        while (rayQueryProceedEXT(rq)) {
            if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
                uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
                if (g_pt_mirror_view && (rt_records[record].flags & 1024u) != 0u) {
                    continue;
                }
                rayQueryConfirmIntersectionEXT(rq);
            }
        }
        if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
            return false;
        }
        float t = rayQueryGetIntersectionTEXT(rq, true);
        uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
        RtSurfaceHit hs = RtFullSurface(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, true), rayQueryGetIntersectionBarycentricsEXT(rq, true),
                                        rayQueryGetIntersectionObjectToWorldEXT(rq, true), dir);
        Material m = materials[hs.material];
        if (m.kind == KIND_CONSTANT || m.kind == KIND_SKY) {
            vec4 tex = textureLod(PT_TEX2D(m.albedo), hs.uv, 2.0);
            float a = Saturate(m.albedo_factor.a * tex.a);
            radiance += throughput * SceneRadiance(m.albedo_factor.rgb * tex.rgb * m.params.y, emissive_cap, frame.hdr_output.x > 0.5) * a;
            throughput *= 1.0 - a;
        } else if (m.kind == KIND_GLASS) {
            vec3 I = dir;
            vec3 Rw = reflect(I, hs.normal);
            vec3 h = Rw - I;
            float nv = Saturate(dot(h * inversesqrt(max(dot(h, h), 1.0e-12)), Rw));
            float schlick = exp2(nv * (-5.5546875 * nv - 6.984375));
            float f0 = Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(m.indices.x, 0.75)).x;
            float F = Saturate(f0 + schlick - f0 * schlick);
            float a = Saturate(F * textureLod(PT_TEX2D(m.albedo), hs.uv, 2.0).x);
            if (RtRandom(rng) < a) {
                glass_point = origin + dir * t;
                glass_normal = hs.normal;
                return true;
            }
        }
        if (dot(throughput, throughput) < 1.0e-8) {
            return false;
        }
        t_start = t + 1.0e-3;
        if (!(t_start < t_end)) {
            return false;
        }
    }
    return false;
}

#include "pt_path_trace.glsl"
