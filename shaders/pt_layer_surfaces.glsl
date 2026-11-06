// Shared verbatim by the Original and Enhanced shader families.
float LayerEnd(float t) {
    return t - max(1.0e-3, 2.0e-4 * t);
}

bool FindMirror(vec3 eye, vec3 dir, float t_end, inout float t_mirror, out RtSurfaceHit mirror_hit) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, rt_casters, gl_RayFlagsCullBackFacingTrianglesEXT | gl_RayFlagsNoOpaqueEXT, 4u, eye, 0.0, dir, t_end);
    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
            if (materials[rt_records[record].material].kind == KIND_PARALLAX) {
                rayQueryConfirmIntersectionEXT(rq);
            }
        }
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
        RtSurfaceHit hs = RtFullSurface(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, true), rayQueryGetIntersectionBarycentricsEXT(rq, true),
                                        rayQueryGetIntersectionObjectToWorldEXT(rq, true), dir);
        if (materials[hs.material].kind == KIND_PARALLAX) {
            mirror_hit = hs;
            t_mirror = rayQueryGetIntersectionTEXT(rq, true);
            return true;
        }
    }
    return false;
}

bool FindGlass(vec3 eye, vec3 dir, float t_end, inout float t_glass, out RtSurfaceHit glass_hit) {
    float t_start = 0.0;
    for (int k = 0; k < 3; ++k) {
        rayQueryEXT rq;
        rayQueryInitializeEXT(rq, rt_casters, gl_RayFlagsCullBackFacingTrianglesEXT, kPtLayerMask, eye, t_start, dir, t_end);
        while (rayQueryProceedEXT(rq)) {
            if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
                rayQueryConfirmIntersectionEXT(rq);
            }
        }
        if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
            break;
        }
        float t = rayQueryGetIntersectionTEXT(rq, true);
        uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
        RtSurfaceHit hs = RtFullSurface(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, true), rayQueryGetIntersectionBarycentricsEXT(rq, true),
                                        rayQueryGetIntersectionObjectToWorldEXT(rq, true), dir);
        if (materials[hs.material].kind == KIND_GLASS) {
            glass_hit = hs;
            t_glass = t;
            return true;
        }
        t_start = t + 1.0e-3;
    }
    return false;
}
