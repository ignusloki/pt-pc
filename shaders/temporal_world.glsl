float PreviousDepth(ivec2 p) {
    return PreviousSecond() ? imageLoad(rt_depth_history1, p).x : imageLoad(rt_depth_history0, p).x;
}

vec3 WorldAt(ivec2 pixel, float depth, out float z) {
    z = ViewZ(v, depth);
    vec2 ndc = (vec2(pixel) + 0.5) * v.viewport.zw * 2.0 - 1.0;
    return (v.inv_view * vec4(ndc * v.projection_param.xy * z, z, 1.0)).xyz;
}
