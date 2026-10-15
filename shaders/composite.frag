#version 460

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene_color;
layout(set = 0, binding = 1) uniform sampler2D grain_noise;
layout(set = 0, binding = 2) uniform sampler2D ui_top;

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

float PqEncode(float nits) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float y = pow(clamp(nits / 10000.0, 0.0, 1.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

void main() {
    vec4 sampled = texture(scene_color, in_uv);
    // MetalFX input and the generated frame's UI composition retain scene colour and HDR gain until final output.
    if (params.mode < -0.5) {
        out_color = sampled;
        return;
    }
    vec3 color = sampled.rgb;
    float scene_gain = 1.0;
    if (params.mode < 0.5 && params.hdr.z > 0.5) {
        float gain = texture(grain_noise, in_uv).r;
        float near_white = smoothstep(0.4, 0.8, max(color.r, max(color.g, color.b)));
        scene_gain = pow(1.0 + gain * near_white, 1.0 / 2.2);
    }
    if (params.mode > 0.5) {
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
        sampled.a = mix(sampled.a, 1.0, top.a);
        if (params.grain_offset.w > 0.5) {
            color = pow(max(color, vec3(0.0)), vec3(1.0 / params.brightness));
        } else {
            color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / params.brightness));
        }
        if (params.grain_offset.w > 0.5) {
            color = pow(clamp(color, vec3(0.0), vec3(1.0)), vec3(2.2)) * pow(clamp(sampled.a, 1.0, 8.0), 2.2);
            float strongest = max(color.r, max(color.g, color.b)) * params.hdr.x;
            if (strongest > params.hdr.y) {
                color *= params.hdr.y / strongest;
            }
            if (params.grain_offset.w < 1.5) {
                out_color = vec4(color * (params.hdr.x / 80.0), 1.0);
            } else {
                vec3 bt2020 = mat3(0.627404, 0.069097, 0.0163916,
                                   0.329283, 0.919540, 0.0880132,
                                   0.0433136, 0.0113612, 0.895595) * (color * params.hdr.x);
                out_color = vec4(vec3(PqEncode(bt2020.r), PqEncode(bt2020.g), PqEncode(bt2020.b)), 1.0);
            }
        } else {
            out_color = vec4(color, 1.0);
        }
        return;
    }
    if (params.pad > 0.5 && params.pad < 1.5) {
        color = vec3(dot(color, vec3(0.2126,0.7152,0.0722)));
    } else if (params.pad < 2.5 && params.pad > 1.5) {
        color *= vec3(1.06,1.0,0.92);
    } else if (params.pad < 3.5 && params.pad > 2.5) {
        color *= vec3(0.92,1.0,1.06);
    } else if (params.pad > 3.5) {
        color = mix(vec3(dot(color,vec3(0.2126,0.7152,0.0722))),color,0.35);
    }
    out_color = vec4(mix(color, params.fade.rgb, params.fade.a), params.hdr.z > 0.5 ? mix(scene_gain, 1.0, params.fade.a) : 1.0);
}
