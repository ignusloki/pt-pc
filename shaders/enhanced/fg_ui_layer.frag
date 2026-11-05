#version 460

#include "composite_output.glsl"
#include "film_grain.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_layer;

layout(set = 0, binding = 0) uniform sampler2D scene_color;
layout(set = 0, binding = 1) uniform sampler2D grain_noise;
layout(set = 0, binding = 2) uniform sampler2D ui_top;
layout(set = 0, binding = 3) uniform sampler2D hudless;

layout(push_constant) uniform CompositeParams {
    float sdr_eotf;
    float brightness;
    float mode;
    float pad;
    vec4 fade;
    vec4 grain;
    vec4 grain_offset;
    vec4 hdr;
    vec4 grade;
    vec4 chroma;
    vec4 extra; // blowout, reserved
} params;

void main() {
    vec4 sampled = texture(scene_color, in_uv);
    vec3 color = sampled.rgb;
    vec4 top = params.hdr.w > 0.5 ? texture(ui_top, in_uv) : vec4(0.0);
    if (params.grain.x > 0.0) {
        float peak = params.hdr.z > 0.5 ? max(params.hdr.y / params.hdr.x, 1.0) : 1.0;
        color = ApplyFilmGrain(color, in_uv, gl_FragCoord.xy, params.grain,
                params.grain_offset, grain_noise, params.sdr_eotf, peak, params.chroma.z);
    }
    color = color * (1.0 - top.a) + top.rgb;
    vec3 shown = CompositeDisplayColor(color, params.brightness, params.grain_offset.w, params.hdr.xy, params.sdr_eotf, params.chroma.z);
    vec3 base = texelFetch(hudless, ivec2(gl_FragCoord.xy), 0).rgb;
    float tolerance = params.grain_offset.w > 1.5 ? 1.5 / 1023.0 : 1.5 / 255.0;
    out_layer = CompositeUiLayer(shown, base, tolerance, params.grain_offset.w < 1.5 ? params.chroma.z : 0.0);
}
