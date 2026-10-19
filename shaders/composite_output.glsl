float PqEncode(float nits) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float y = pow(clamp(nits / 10000.0, 0.0, 1.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

vec3 CompositeDisplayColor(vec3 color, float gain, float brightness, float output_mode, vec2 hdr) {
    if (output_mode > 0.5) {
        color = pow(max(color, vec3(0.0)), vec3(1.0 / brightness));
    } else {
        color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / brightness));
    }
    if (output_mode > 0.5) {
        color = pow(clamp(color, vec3(0.0), vec3(1.0)), vec3(2.2)) * pow(clamp(gain, 1.0, 8.0), 2.2);
        float strongest = max(color.r, max(color.g, color.b)) * hdr.x;
        if (strongest > hdr.y) {
            color *= hdr.y / strongest;
        }
        if (output_mode < 1.5) {
            return color * (hdr.x / 80.0);
        }
        vec3 bt2020 = mat3(0.627404, 0.069097, 0.0163916,
                           0.329283, 0.919540, 0.0880132,
                           0.0433136, 0.0113612, 0.895595) * (color * hdr.x);
        return vec3(PqEncode(bt2020.r), PqEncode(bt2020.g), PqEncode(bt2020.b));
    }
    return color;
}

vec4 CompositeUiLayer(vec3 shown, vec3 base, float tolerance) {
    vec3 change = shown - base;
    if (all(lessThanEqual(abs(change), vec3(tolerance)))) {
        return vec4(max(change, vec3(0.0)), 0.0);
    }
    float a = max(abs(change.r), max(abs(change.g), abs(change.b)));
    for (int c = 0; c < 3; ++c) {
        if (base[c] > 0.0) {
            a = max(a, 1.0 - shown[c] / base[c]);
        }
    }
    a = clamp(a, 0.0, 1.0);
    return vec4(max(shown - (1.0 - a) * base, vec3(0.0)), a);
}
