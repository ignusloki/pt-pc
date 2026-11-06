vec3 QuantizeDither(vec3 c, float salt) {
    vec3 seed = fract((vec3(gl_FragCoord.xyx) + salt) * vec3(0.1031, 0.1030, 0.0973));
    seed += dot(seed, seed.yxz + 33.33);
    return c + (fract((seed.x + seed.y) * seed.z) - 0.5) / 255.0;
}
