// Shared verbatim by the Original and Enhanced shader families.
layout(set = 0, binding = 0) uniform PT_TEXTURE2D_ARRAY textures[];
layout(std430, set = 1, binding = 0) readonly buffer UiDraws {
    UiDraw draws[];
};
layout(set = 1, binding = 1) uniform sampler2D scene_color;

vec4 Sample(uint packed, vec2 uv) {
    uint index = packed & 0x3FFFFFFFu;
    vec2 size = vec2(textureSize(PT_TEX2D(index), 0));
    float lod = log2(max(max(length(dFdx(uv) * size), length(dFdy(uv) * size)), 1e-6));
    if ((packed & 0xC0000000u) != 0u) {
        vec2 half_texel = 0.5 / vec2(textureSize(PT_TEX2D(index), 0));
        if ((packed & 0x80000000u) != 0u) {
            uv.x = clamp(uv.x, half_texel.x, 1.0 - half_texel.x);
        }
        if ((packed & 0x40000000u) != 0u) {
            uv.y = clamp(uv.y, half_texel.y, 1.0 - half_texel.y);
        }
    }
    return textureLod(PT_TEX2D(index), uv, lod);
}

vec4 ShadeMaterial(UiDraw d) {
    vec2 base_uv = (in_uv - d.m[0].xy) * d.m[1].xy + d.m[0].xy + d.m[0].zw;
    vec2 layer_uv = (in_uv - d.m[1].zw) * d.m[2].zw + d.m[1].zw + d.m[2].xy;
    vec2 mask_uv = (in_uv - d.m[3].xy) * d.m[4].xy + d.m[3].xy + d.m[3].zw;
    vec2 screen = (gl_FragCoord.xy - d.screen.xy) * d.screen.z;
    vec2 screen_uv = (screen - d.m[4].zw) * d.m[5].zw + d.m[4].zw + d.m[5].xy;
    vec4 base = Sample(d.textures.x, base_uv);
    vec4 layer = Sample(d.textures.y, layer_uv);
    vec4 mask = Sample(d.textures.z, mask_uv);
    vec4 noise = Sample(d.textures.w, screen_uv);
    vec3 rgb = clamp(mix(base.rgb, layer.rgb, d.m[6].y), 0.0, 1.0) * in_color.rgb;
    float alpha = clamp(mix(base.a, layer.a, d.m[6].y), 0.0, 1.0) * (d.m[6].z * (mask.g - 1.0) + 1.0);
    alpha = clamp(alpha * (d.m[6].w * (noise.g - 1.0) + 1.0), 0.0, 1.0) * in_color.a;
    return vec4(rgb, alpha);
}

vec4 ShadeText(UiDraw d) {
    float coverage = Sample(d.textures.x, in_uv).r;
    return vec4(in_color.rgb, coverage * in_color.a);
}
