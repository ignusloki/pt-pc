vec3 Clamp(vec3 c) {
    if (any(isnan(c)) || any(isinf(c))) {
        return vec3(0.0);
    }
    float bright = Luma709(c) * v.exposure.x;
    return bright > pass.f0.w ? c * (pass.f0.w / bright) : c;
}
