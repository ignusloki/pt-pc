vec2 PtCasterUV(uint record, uint primitive, vec2 bary) {
    RtRecord r = rt_records[record];
    RtIndexRef ib = RT_INDEX_REF(r.indices);
    RtVertexRef vb = RT_VERTEX_REF(r.vertices);
    uint base = r.first_index + 3u * primitive;
    uint i0 = (RT_INDEX(ib, base) + uint(r.vertex_offset)) * 22u + 10u;
    uint i1 = (RT_INDEX(ib, base + 1u) + uint(r.vertex_offset)) * 22u + 10u;
    uint i2 = (RT_INDEX(ib, base + 2u) + uint(r.vertex_offset)) * 22u + 10u;
    return vec2(RT_VERTEX(vb, i0), RT_VERTEX(vb, i0 + 1u)) * (1.0 - bary.x - bary.y) +
           vec2(RT_VERTEX(vb, i1), RT_VERTEX(vb, i1 + 1u)) * bary.x + vec2(RT_VERTEX(vb, i2), RT_VERTEX(vb, i2 + 1u)) * bary.y;
}
