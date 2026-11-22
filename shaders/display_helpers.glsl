const float kStreetMaskLod = 7.0;
float LightMaskLod(uint flags) {
    return (flags & 16u) != 0u ? kStreetMaskLod : 0.0;
}
vec2 LightMaskUv(uint flags, vec2 uv) {
    return (flags & 16u) != 0u ? vec2(0.5) : uv;
}

bool HdrOutput() {
    return frame.display.x > 0.5;
}

vec2 MbLoad(vec2 stored) {
    return HdrOutput() ? stored : 2.0 * stored - 1.0;
}
vec2 MbStore(vec2 velocity) {
    return HdrOutput() ? velocity : 0.5 * velocity + 0.5;
}

vec3 PostLight(vec3 encoded) {
    return pow(max(encoded, vec3(0.0)), vec3(2.2));
}
vec3 PostEncode(vec3 light) {
    return pow(max(light, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 LightCap(vec3 c, float cap) {
    if (!HdrOutput()) {
        return min(c, vec3(cap));
    }
    c = max(c, vec3(0.0));
    float peak = max(c.r, max(c.g, c.b));
    return peak > 60000.0 ? c * (60000.0 / peak) : c;
}
