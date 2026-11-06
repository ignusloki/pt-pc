vec4 ShadeString(UiDraw d) {
    vec4 t = Sample(d.textures.x, d.extra.xy + in_uv);
    vec3 c = clamp(0.1 * in_color.rgb, 0.0, 1.0);
    vec3 encoded = mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, 12.92 * c, lessThanEqual(c, vec3(0.0031308)));
    return vec4(encoded, clamp(d.extra.w * t.a * in_color.a, 0.0, 1.0));
}
