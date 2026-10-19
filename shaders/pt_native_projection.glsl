bool PtProjectionStaticCandidate(uint flags, bool spot, float near_plane, float fitting_reach) {
    return (flags & 4u) == 0u && ((flags >> 4u) & 63u) == 0u && spot && near_plane > fitting_reach;
}

bool PtProjectionClipsTriangle(vec3 light, vec3 axis, float near_plane, vec3 a, vec3 b, vec3 c) {
    vec3 da = a - light;
    vec3 db = b - light;
    vec3 dc = c - light;
    float near_squared = near_plane * near_plane;
    return dot(da, axis) < near_plane && dot(db, axis) < near_plane && dot(dc, axis) < near_plane &&
           dot(da, da) < near_squared && dot(db, db) < near_squared && dot(dc, dc) < near_squared;
}
