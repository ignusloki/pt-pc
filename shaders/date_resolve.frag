#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (pass.ids.z == 1u) {
        vec3 encoded = SrgbEncode(max(ImgFetch(pass.ids.x, pixel).rgb, vec3(0.0)));
        out_color = vec4(encoded, Luma601(encoded));
        return;
    }
    vec3 current = SrgbDecode(ImgFetch(pass.ids.x, pixel).rgb);
    float weight = pass.f0.x;
    if (weight >= 1.0) {
        out_color = vec4(current, 1.0);
        return;
    }
    vec3 history = ImgFetch(pass.ids.y, pixel).rgb;
    if (pass.f0.y > 0.5) {
        ivec2 last = ivec2(ImgSize(pass.ids.x)) - 1;
        vec3 lo = current;
        vec3 hi = current;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vec3 c = SrgbDecode(ImgFetch(pass.ids.x, clamp(pixel + ivec2(x, y), ivec2(0), last)).rgb);
                lo = min(lo, c);
                hi = max(hi, c);
            }
        }
        history = clamp(history, lo, hi);
    }
    out_color = vec4(mix(history, current, weight), 1.0);
}
