vec3 Upsample(bool glossy, ivec2 pixel, ivec2 size, float z, vec3 n, View v) {
    ivec2 half_size = (size + 1) / 2;
    vec2 at = vec2(pixel) * 0.5;
    ivec2 base = ivec2(floor(at));
    vec2 f = at - vec2(base);
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;
    vec3 nearest = vec3(0.0);
    float nearest_error = 1.0e9;
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i & 1, i >> 1);
        ivec2 p = min(base + o, half_size - 1);
        ivec2 full = min(p * 2, size - 1);
        float dk = ImgFetch(IMG_DEPTH, full).x;
        if (dk <= 0.0) {
            continue;
        }
        vec4 c = glossy ? imageLoad(rt_glossy_pong, p) : imageLoad(rt_gi_ping, p);
        if (c.a <= 0.0) {
            continue;
        }
        float error = abs(ViewZ(v, dk) - z) / z;
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) + 1.0e-3;
        w *= exp(-error / 0.02);
        w *= pow(max(dot(n, DecodeNormal(ImgFetch(IMG_NORMAL, full).xyz)), 0.0), 8.0);
        sum += w * c.rgb;
        weight_sum += w;
        if (error < nearest_error) {
            nearest_error = error;
            nearest = c.rgb;
        }
    }
    return weight_sum > 1.0e-4 ? sum / weight_sum : nearest;
}
