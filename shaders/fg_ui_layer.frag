#version 460

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_layer;

layout(set = 0, binding = 0) uniform sampler2D scene_color;
layout(set = 0, binding = 1) uniform sampler2D grain_noise;
layout(set = 0, binding = 2) uniform sampler2D ui_top;
layout(set = 0, binding = 3) uniform sampler2D hudless;

layout(push_constant) uniform CompositeParams {
    float exposure;
    float brightness;
    float mode;
    float pad;
    vec4 fade;
    vec4 grain;
    vec4 grain_offset;
    vec4 hdr;
} params;

void main() {
    vec4 sampled = texture(scene_color, in_uv);
    vec3 color = sampled.rgb;
    vec4 top = params.hdr.w > 0.5 ? texture(ui_top, in_uv) : vec4(0.0);
    if (params.grain.x > 0.0) {
        float y = abs(dot(color, vec3(0.299, 0.587, 0.114)));
        float strength = params.grain.z;
        float alpha;
        if (params.grain.y > 0.0) {
            alpha = clamp(pow(y, 0.08) * (1.0 - pow(y, 0.12)) * strength * 0.45, 0.0, 1.0);
        } else {
            float p = pow(y, 0.7);
            alpha = clamp(p * (p * -strength * 0.45 + strength * 0.45), 0.0, 1.0);
        }
        float across = params.grain_offset.z > 0.0 ? params.grain_offset.z : 1.0;
        vec2 grain_uv = vec2((in_uv.x - 0.5) * across + 0.5, in_uv.y);
        vec3 noise = clamp(textureLod(grain_noise, 3.0 * (params.grain_offset.xy + grain_uv), log2(1536.0 / 1080.0)).rgb, 0.0, 1.0);
        color = mix(color, noise, alpha);
    }
    color = color * (1.0 - top.a) + top.rgb;
    vec3 shown = pow(clamp(color, 0.0, 1.0), vec3(1.0 / params.brightness));
    vec3 base = texelFetch(hudless, ivec2(gl_FragCoord.xy), 0).rgb;
    vec3 change = shown - base;
    if (all(lessThanEqual(abs(change), vec3(1.5 / 255.0)))) {
        out_layer = vec4(max(change, vec3(0.0)), 0.0);
        return;
    }
    float a = max(abs(change.r), max(abs(change.g), abs(change.b)));
    for (int c = 0; c < 3; ++c) {
        if (base[c] > 0.0) {
            a = max(a, 1.0 - shown[c] / base[c]);
        }
    }
    a = clamp(a, 0.0, 1.0);
    out_layer = vec4(max(shown - (1.0 - a) * base, vec3(0.0)), a);
}
