#version 460
#include "common.glsl"
#include "post_filter.glsl"

#include "banding_depth_linear_shared.glsl"

void main() {
    uint source = pass.ids.x;
    ivec2 size = ivec2(ImgSize(IMG_DEPTH));
    vec2 texel = 1.0 / vec2(size);
    vec2 uv = clamp((floor(gl_FragCoord.xy) + 1.0) * texel, 0.0, 1.0);
    vec4 center = Img(source, SMP_LINEAR_CLAMP, uv);
    if (DepthLinear(uv, size) > 1.0e-6) {
        out_color = center;
        return;
    }
    vec3 c = PostLinearSample(source, uv).rgb;
    float k = 1.0;
    for (int level = 0; level < 8; ++level) {
        vec2 o = texel * k;
        k *= 1.5;
        vec2 taps[4] = vec2[](clamp(uv - vec2(o.x, 0.0), 0.0, 1.0), clamp(uv + vec2(o.x, 0.0), 0.0, 1.0), clamp(uv - vec2(0.0, o.y), 0.0, 1.0),
                              clamp(uv + vec2(0.0, o.y), 0.0, 1.0));
        bool covered = false;
        for (int i = 0; i < 4; ++i) {
            covered = covered || DepthLinear(taps[i], size) > 1.0e-6;
        }
        if (covered) {
            continue;
        }
        vec3 sum = c;
        for (int i = 0; i < 4; ++i) {
            sum += PostLinearSample(source, taps[i]).rgb;
        }
        c = sum * 0.2;
    }
    out_color = vec4(pass.f1.z > 0.0 ? SrgbEncode(max(c, vec3(0.0))) : Saturate(c), 1.0);
}
