vec3 ScreenTonemap(vec3 x, vec3 bloom) {
    vec3 x2 = x * x;
    vec3 r = clamp(x2 + bloom - x2 * bloom, 0.0, 1.0);
    return clamp(r * inversesqrt(max(vec3(1.0 / 512.0), r)), 0.0, 1.0);
}
