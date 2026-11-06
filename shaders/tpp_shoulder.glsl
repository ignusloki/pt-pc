vec3 TppShoulder(vec3 x) {
    float a = pass.f0.x;
    float t = pass.f0.y;
    float c = pass.f0.z;
    vec3 curve = t + c - 1.0 / (a * (max(x, vec3(t)) - t + c));
    return mix(curve, x, vec3(lessThanEqual(x, vec3(t))));
}
