// Shared verbatim by the Original and Enhanced shader families.
#extension GL_EXT_ray_query : require
#ifndef PT_D3D12
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#endif
#include "common.glsl"
#include "rt_shadow.glsl"
#include "rt_full.glsl"
#include "pt_path.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

#include "pt_images.glsl"

View v;

float Cutoff() {
    return 1.0e-4 / max(v.exposure.x, 1.0e-12);
}

struct PrimarySurface {
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec2 uv;
    uint material;
};

PrimarySurface PrimaryHit(uint record, uint primitive, vec2 bary, mat4x3 to_world, vec3 ray) {
    RtRecord r = rt_records[record];
    RtIndexRef ib = RT_INDEX_REF(r.indices);
    RtVertexRef vb = RT_VERTEX_REF(r.vertices);
    uint base = r.first_index + 3u * primitive;
    uint a = (RT_INDEX(ib, base) + uint(r.vertex_offset)) * 22u;
    uint b = (RT_INDEX(ib, base + 1u) + uint(r.vertex_offset)) * 22u;
    uint c = (RT_INDEX(ib, base + 2u) + uint(r.vertex_offset)) * 22u;
    vec3 w = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
    PrimarySurface s;
    s.uv = vec2(RT_VERTEX(vb, a + 10u), RT_VERTEX(vb, a + 11u)) * w.x + vec2(RT_VERTEX(vb, b + 10u), RT_VERTEX(vb, b + 11u)) * w.y +
           vec2(RT_VERTEX(vb, c + 10u), RT_VERTEX(vb, c + 11u)) * w.z;
    vec3 n = vec3(RT_VERTEX(vb, a + 3u), RT_VERTEX(vb, a + 4u), RT_VERTEX(vb, a + 5u)) * w.x +
             vec3(RT_VERTEX(vb, b + 3u), RT_VERTEX(vb, b + 4u), RT_VERTEX(vb, b + 5u)) * w.y +
             vec3(RT_VERTEX(vb, c + 3u), RT_VERTEX(vb, c + 4u), RT_VERTEX(vb, c + 5u)) * w.z;
    vec3 t = vec3(RT_VERTEX(vb, a + 6u), RT_VERTEX(vb, a + 7u), RT_VERTEX(vb, a + 8u)) * w.x +
             vec3(RT_VERTEX(vb, b + 6u), RT_VERTEX(vb, b + 7u), RT_VERTEX(vb, b + 8u)) * w.y +
             vec3(RT_VERTEX(vb, c + 6u), RT_VERTEX(vb, c + 7u), RT_VERTEX(vb, c + 8u)) * w.z;
    float sign_w = RT_VERTEX(vb, a + 9u) < 0.0 ? -1.0 : 1.0;
    vec3 nw = mat3(to_world) * n;
    vec3 tw = mat3(to_world) * t;
    nw = dot(nw, nw) > 1.0e-12 ? normalize(nw) : -ray;
    tw = dot(tw, tw) > 1.0e-12 ? normalize(tw) : vec3(1.0, 0.0, 0.0);
    s.bitangent = sign_w * normalize(cross(nw, tw) + vec3(0.0, 0.0, 1.0e-9));
    s.tangent = tw;
    s.normal = nw;
    if (dot(nw, ray) > 0.0) {
        s.normal = -nw;
    }
    s.material = r.material;
    return s;
}

PtMaterial PrimaryMaterial(Material m, PrimarySurface ps, bool front, out vec3 n) {
    PtMaterial s;
    s.albedo = (m.flags & MAT_CONSTANT_COLOR) != 0u ? m.albedo_factor.rgb : textureLod(PT_TEX2D(m.albedo), ps.uv, 0.0).rgb;
    n = ps.normal;
    if ((m.flags & MAT_NORMAL_MAP) != 0u) {
        vec4 nt = textureLod(PT_TEX2D(m.normal), ps.uv, 0.0);
        vec2 nxy = vec2(nt.w, nt.y) * 2.0 - 1.0;
        float nz = sqrt(Saturate(1.0 - dot(nxy, nxy)) + 1.00016594e-4);
        vec3 geo = front ? ps.normal : -ps.normal;
        vec3 mapped = normalize(ps.tangent * nxy.x + ps.bitangent * nxy.y + geo * nz);
        n = dot(mapped, ps.normal) > 0.0 ? mapped : ps.normal;
    }
    vec4 srm = (m.flags & MAT_NORMAL_WAVE) != 0u ? vec4(m.extra.x, m.extra.y, 0.0, 0.0) : textureLod(PT_TEX2D(m.specular), ps.uv, 0.0);
    s.specular = srm.r;
    s.roughness = srm.g;
    s.material_u = m.indices.x;
    s.translucency = (m.flags & MAT_TRANSLUCENT_TEX) != 0u ? textureLod(PT_TEX2D(m.aux1), ps.uv, 0.0).y
                     : (m.flags & MAT_NORMAL_WAVE) != 0u ? m.extra.z : 0.0;
    PtMaterialRows(s);
    return s;
}
