#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) flat in uint in_draw;

layout(location = 0) out vec4 out_color;

struct UiDraw {
    uvec4 textures;
    vec4 m[7];
    vec4 screen;
    vec4 extra;
    vec4 extra2;
};

layout(push_constant) uniform UiPush {
    vec2 inverse_extent;
    uint draw;
} push;

#include "bindless.glsl"
#include "ui_sprite_sample_shared.glsl"

#include "ui_glyph_border.glsl"

vec3 SceneDisplay(vec2 uv, UiDraw d) {
    vec3 hdr = texture(scene_color, uv).rgb * d.extra2.x;
    return pow(clamp(hdr, 0.0, 1.0), vec3(1.0 / max(d.extra2.y, 0.001)));
}

vec4 ShadeNoise(UiDraw d) {
    float phase = d.extra.x;
    float across = d.extra2.z > 0.0 ? d.extra2.z : 1.0;
    vec2 noise_uv = vec2((in_uv.x - 0.5) * across + 0.5, in_uv.y);
    vec4 normal = Sample(d.textures.y, vec2(phase + noise_uv.x, d.extra.y + noise_uv.y));
    vec2 uv = clamp(in_uv + vec2((-0.001 + normal.w * 0.002) / across, -0.03 + normal.y * 0.06), vec2(0.0), vec2(1.0));
    vec3 screen = SceneDisplay(uv, d);
    float noise = Sample(d.textures.x, vec2(phase + noise_uv.x, noise_uv.y)).x;
    vec3 rgb;
    rgb.r = clamp(clamp(d.extra.z * 0.4 + (noise - screen.r) * 0.1 + screen.r, 0.0, 1.0) * in_color.r, 0.0, 1.0);
    rgb.g = clamp(((noise - screen.g) * 0.1 + screen.g) * in_color.g, 0.0, 1.0);
    rgb.b = clamp(((noise - screen.b) * 0.1 + screen.b) * in_color.b, 0.0, 1.0);
    return vec4(rgb, clamp(d.extra.w * in_color.a, 0.0, 1.0));
}

#include "ui_string_shading.glsl"

void main() {
    UiDraw d = draws[in_draw];
    uint mode = uint(d.screen.w + 0.5);
    if (mode == 1u) {
        out_color = ShadeText(d);
    } else if (mode == 2u) {
        out_color = ShadeNoise(d);
    } else if (mode == 3u) {
        out_color = ShadeString(d);
    } else if (mode == 4u) {
        out_color = in_color;
    } else if (mode == 5u) {
        out_color = ShadeBorder(d);
    } else {
        out_color = ShadeMaterial(d);
    }
}
